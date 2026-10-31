// Based on LovyanGFX 1.1.16 src/lgfx/v1/platforms/esp32s3/Bus_RGB.cpp
// (FreeBSD licence, lovyan03 and contributors). The LCD set-up is theirs;
// the bounce buffers and their interrupt are new.
#include "rgb_bounce.h"

#include <esp_heap_caps.h>
#include <esp_intr_alloc.h>
#include <esp_log.h>
#include <esp_rom_gpio.h>
#include <hal/gdma_ll.h>
#include <hal/gpio_hal.h>
#include <hal/lcd_ll.h>
#include <soc/gdma_channel.h>
#include <soc/gdma_struct.h>
#include <soc/lcd_cam_struct.h>
#include <soc/lcd_periph.h>
#include <soc/periph_defs.h>
#include <string.h>
#include <rom/cache.h>

namespace {

constexpr size_t kMaxDescLen = 4096 - 64;
constexpr uint32_t kDescOwnerDma = 0x80000000;
constexpr uint32_t kDescEof = 0x40000000;

void pinSignal(uint32_t pin, uint32_t sig) {
  gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[pin], PIN_FUNC_GPIO);
  gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
  esp_rom_gpio_connect_out_signal(pin, sig, false, false);
}

}  // namespace

// Copies a part of the frame into a bounce buffer with the DMA engine, so it
// costs no CPU time; the CPU copies only if the DMA queue is full.
IRAM_ATTR void BusRgbBounce::fill(int buf, int chunk) {
  uint8_t *src = _frame_buffer + chunk * _chunk_bytes;
  if (esp_async_memcpy(_copier, _bounce[buf], src, _chunk_bytes, nullptr, nullptr) != ESP_OK) {
    memcpy(_bounce[buf], src, _chunk_bytes);
  }
}

// A bounce buffer has gone out to the LCD: load the next part of the frame.
IRAM_ATTR void BusRgbBounce::eofIsr(void *arg) {
  auto *me = static_cast<BusRgbBounce *>(arg);
  auto &ch = GDMA.channel[me->_dma_ch].out;
  if (!ch.int_st.out_eof) return;
  ch.int_clr.out_eof = 1;
  const int buf = me->_next_buf;
  me->fill(buf, me->_next_chunk);
  me->_next_buf = (buf + 1) % kBounceBufs;
  me->_next_chunk = (me->_next_chunk + 1) % me->_chunks;
}

// Start of each frame: restart the DMA at the top so any slip lasts at most
// one frame. The buffers already hold the top of the next frame: the refills
// wrap around from the bottom.
IRAM_ATTR void BusRgbBounce::vsyncIsr(void *arg) {
  auto *me = static_cast<BusRgbBounce *>(arg);
  auto *dev = &LCD_CAM;
  const uint32_t status = dev->lc_dma_int_st.val & 0x03;
  dev->lc_dma_int_clr.val = status;
  if (!(status & LCD_LL_EVENT_VSYNC_END)) return;

  auto &ch = GDMA.channel[me->_dma_ch].out;
  ch.conf0.out_rst = 1;
  ch.conf0.out_rst = 0;
  ch.int_clr.out_eof = 1;
  ch.link.addr = (uintptr_t)&me->_restart;
  ch.link.start = 1;
  me->_next_buf = 0;
  me->_next_chunk = kBounceBufs;
}

void BusRgbBounce::writeBack(int y, int h) {
  const size_t line = _cfg.panel->width() * 2;
  Cache_WriteBack_Addr((uint32_t)(_frame_buffer + y * line), h * line);
}

// The interrupts run on the core they are set up on: set up on core 0 and
// leave core 1 to LVGL and touch.
bool BusRgbBounce::init() {
  struct Job {
    BusRgbBounce *bus;
    SemaphoreHandle_t done;
    bool ok;
  } job = {this, xSemaphoreCreateBinary(), false};
  xTaskCreatePinnedToCore(
      [](void *arg) {
        auto *j = static_cast<Job *>(arg);
        j->ok = j->bus->initHere();
        xSemaphoreGive(j->done);
        vTaskDelete(nullptr);
      },
      "rgb_init", 4096, &job, configMAX_PRIORITIES - 1, nullptr, 0);
  xSemaphoreTake(job.done, portMAX_DELAY);
  vSemaphoreDelete(job.done);
  return job.ok;
}

bool BusRgbBounce::initHere() {
  const int w = _cfg.panel->width();
  const int h = _cfg.panel->height();
  constexpr int pixel_bytes = 2;  // RGB565
  if (h % (kBounceBufs * kBounceLines)) {
    ESP_LOGE("rgb", "panel height %d is not a multiple of %d", h, kBounceBufs * kBounceLines);
    return false;
  }

  // Dummy i80 bus: lets ESP-IDF power up and clock the LCD peripheral.
  esp_lcd_i80_bus_config_t bus_config;
  memset(&bus_config, 0, sizeof(bus_config));
  bus_config.dc_gpio_num = _cfg.pin_vsync;
  bus_config.wr_gpio_num = _cfg.pin_pclk;
  bus_config.clk_src = LCD_CLK_SRC_PLL160M;
  for (int i = 0; i < 16; ++i) bus_config.data_gpio_nums[i ^ 8] = _cfg.pin_data[i];
  bus_config.bus_width = 16;
  bus_config.max_transfer_bytes = 4092;
  if (esp_lcd_new_i80_bus(&bus_config, &_i80_bus) != ESP_OK) return false;

  auto *dev = &LCD_CAM;
  {
    static constexpr uint8_t rgb565sig[] = {8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7};
    const auto &sigs = lcd_periph_signals.panels[_cfg.port];
    for (int i = 0; i < 16; i++) pinSignal(_cfg.pin_data[i], sigs.data_sigs[rgb565sig[i]]);
    pinSignal(_cfg.pin_henable, sigs.de_sig);
    pinSignal(_cfg.pin_hsync, sigs.hsync_sig);
    pinSignal(_cfg.pin_vsync, sigs.vsync_sig);
    pinSignal(_cfg.pin_pclk, sigs.pclk_sig);
  }

  _dma_ch = lgfx::search_dma_out_ch(SOC_GDMA_TRIG_PERIPH_LCD0);
  if (_dma_ch < 0) {
    ESP_LOGE("rgb", "no DMA channel");
    return false;
  }
  auto &ch = GDMA.channel[_dma_ch].out;
  ch.peri_sel.sel = SOC_GDMA_TRIG_PERIPH_LCD0;
  {
    typeof(ch.conf0) conf0;
    conf0.val = 0;
    conf0.out_eof_mode = 1;  // EOF once the data has left the DMA's FIFO
    conf0.outdscr_burst_en = 1;
    conf0.out_data_burst_en = 1;
    ch.conf0.val = conf0.val;
    ch.conf1.val = 0;
  }

  // Buffers: the frame in PSRAM, the bounce buffers in internal RAM.
  const size_t fb_len = (size_t)w * h * pixel_bytes;
  _chunk_bytes = (size_t)w * kBounceLines * pixel_bytes;
  _chunks = h / kBounceLines;
  // 64-byte aligned for the DMA copier (whole cache lines of PSRAM).
  _frame_buffer = (uint8_t *)heap_caps_aligned_calloc(64, 1, fb_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  for (auto &b : _bounce) b = (uint8_t *)heap_caps_calloc(1, _chunk_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  const int per_buf = (_chunk_bytes + kMaxDescLen - 1) / kMaxDescLen;
  _desc = (dma_descriptor_t *)heap_caps_calloc(kBounceBufs * per_buf, sizeof(dma_descriptor_t), MALLOC_CAP_DMA);
  bool ok = _frame_buffer && _desc;
  for (auto *b : _bounce) ok = ok && b;
  if (!ok) {
    ESP_LOGE("rgb", "out of memory");
    return false;
  }

  // A ring of descriptors over the bounce buffers; the last one of each
  // buffer raises EOF.
  dma_descriptor_t *d = _desc;
  for (int buf = 0; buf < kBounceBufs; ++buf) {
    size_t left = _chunk_bytes;
    uint8_t *p = _bounce[buf];
    while (left) {
      const size_t n = left > kMaxDescLen ? kMaxDescLen : left;
      left -= n;
      *(uint32_t *)d = n | n << 12 | kDescOwnerDma | (left ? 0 : kDescEof);
      d->buffer = p;
      d->next = d + 1;
      p += n;
      ++d;
    }
  }
  _desc[kBounceBufs * per_buf - 1].next = _desc;

  // Restart point for each frame. The LCD's own FIFO still holds the first
  // pixels from before the DMA reset, so skip those (as Bus_RGB does).
  memcpy(&_restart, _desc, sizeof(_restart));
  const int skip = (GDMA_LL_L2FIFO_BASE_SIZE + 1) * pixel_bytes;
  _restart.buffer = (uint8_t *)_restart.buffer + skip;
  _restart.dw0.length -= skip;
  _restart.dw0.size -= skip;

  async_memcpy_config_t copier_cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
  copier_cfg.backlog = 4 * kBounceBufs;
  copier_cfg.psram_trans_align = 64;
  copier_cfg.sram_trans_align = 4;
  if (esp_async_memcpy_install(&copier_cfg, &_copier) != ESP_OK) {
    ESP_LOGE("rgb", "no DMA copier");
    return false;
  }
  for (int i = 0; i < kBounceBufs; ++i) memcpy(_bounce[i], _frame_buffer + i * _chunk_bytes, _chunk_bytes);
  _next_buf = 0;
  _next_chunk = kBounceBufs;
  ch.link.addr = (uintptr_t)_desc;
  ch.link.start = 1;

  // Timings, exactly as Bus_RGB.
  const uint32_t hsw = _cfg.hsync_pulse_width, hbp = _cfg.hsync_back_porch, hfp = _cfg.hsync_front_porch;
  const uint32_t vsw = _cfg.vsync_pulse_width, vbp = _cfg.vsync_back_porch, vfp = _cfg.vsync_front_porch;

  uint32_t div_a, div_b, div_n, clkcnt;
  lgfx::calcClockDiv(&div_a, &div_b, &div_n, &clkcnt, 240 * 1000 * 1000,
                     std::min<uint32_t>(_cfg.freq_write, 40000000u));
  typeof(dev->lcd_clock) lcd_clock;
  lcd_clock.val = 0;
  lcd_clock.lcd_clkcnt_n = std::max<uint32_t>(1u, clkcnt - 1);
  lcd_clock.lcd_clk_equ_sysclk = (clkcnt == 1);
  lcd_clock.lcd_ck_idle_edge = false;
  lcd_clock.lcd_ck_out_edge = _cfg.pclk_idle_high;
  lcd_clock.lcd_clkm_div_num = div_n;
  lcd_clock.lcd_clkm_div_b = div_b;
  lcd_clock.lcd_clkm_div_a = div_a;
  lcd_clock.lcd_clk_sel = 2;  // 240 MHz
  lcd_clock.clk_en = true;
  dev->lcd_clock.val = lcd_clock.val;

  typeof(dev->lcd_user) lcd_user;
  lcd_user.val = 0;
  lcd_user.lcd_always_out_en = true;
  lcd_user.lcd_2byte_en = true;
  lcd_user.lcd_dout = 1;
  lcd_user.lcd_update = 1;
  lcd_user.lcd_reset = 1;
  lcd_user.lcd_dummy_cyclelen = 3;
  dev->lcd_user.val = lcd_user.val;

  typeof(dev->lcd_misc) lcd_misc;
  lcd_misc.val = 0;
  lcd_misc.lcd_afifo_reset = true;
  lcd_misc.lcd_next_frame_en = true;
  lcd_misc.lcd_bk_en = true;
  dev->lcd_misc.val = lcd_misc.val;

  typeof(dev->lcd_ctrl) lcd_ctrl;
  lcd_ctrl.val = 0;
  lcd_ctrl.lcd_hb_front = hbp + hsw - 1;
  lcd_ctrl.lcd_va_height = h - 1;
  lcd_ctrl.lcd_vt_height = vsw + vbp + h + vfp - 1;
  lcd_ctrl.lcd_rgb_mode_en = true;
  dev->lcd_ctrl.val = lcd_ctrl.val;

  typeof(dev->lcd_ctrl1) lcd_ctrl1;
  lcd_ctrl1.val = 0;
  lcd_ctrl1.lcd_vb_front = vbp + vsw - 1;
  lcd_ctrl1.lcd_ha_width = w - 1;
  lcd_ctrl1.lcd_ht_width = hsw + hbp + w + hfp - 1;
  dev->lcd_ctrl1.val = lcd_ctrl1.val;

  typeof(dev->lcd_ctrl2) lcd_ctrl2;
  lcd_ctrl2.val = 0;
  lcd_ctrl2.lcd_vsync_width = vsw - 1;
  lcd_ctrl2.lcd_vsync_idle_pol = _cfg.vsync_polarity;
  lcd_ctrl2.lcd_hs_blank_en = true;
  lcd_ctrl2.lcd_hsync_width = hsw - 1;
  lcd_ctrl2.lcd_hsync_idle_pol = _cfg.hsync_polarity;
  lcd_ctrl2.lcd_de_idle_pol = _cfg.de_idle_high;
  dev->lcd_ctrl2.val = lcd_ctrl2.val;

  // Interrupts. Not IRAM-safe on purpose: they read PSRAM, so they must wait
  // while flash is being written (NVS, OTA) and the cache is off.
  dev->lc_dma_int_ena.val = 1;
  esp_intr_alloc_intrstatus(lcd_periph_signals.panels[_cfg.port].irq_id,
                            ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_SHARED,
                            (uint32_t)&dev->lc_dma_int_st, LCD_LL_EVENT_VSYNC_END, vsyncIsr, this,
                            &_lcd_intr);
  if (esp_intr_alloc(ETS_DMA_OUT_CH0_INTR_SOURCE + _dma_ch, ESP_INTR_FLAG_INTRDISABLED, eofIsr, this,
                     &_dma_intr) != ESP_OK) {
    ESP_LOGE("rgb", "no DMA interrupt");
    return false;
  }
  ch.int_clr.val = ~0u;
  ch.int_ena.out_eof = 1;
  esp_intr_enable(_dma_intr);
  esp_intr_enable(_lcd_intr);

  dev->lcd_user.lcd_update = 1;
  dev->lcd_user.lcd_start = 1;
  return true;
}
