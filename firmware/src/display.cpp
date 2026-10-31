#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <lvgl.h>

#include "display.h"

#include "config.h"

// ---- Texts and settings ----
// Pin map and timings per board, from Elecrow's reference sketches and the
// 7.0" V3.0 schematic. The two boards wire the 16 colour lines, DE and VSYNC
// to different pins, so a 5" build on a 7" board shows lines and flicker.
struct RgbPins {
  int8_t b[5], g[6], r[5];
  int8_t de, vsync, hsync, pclk;
};
struct RgbTiming {
  uint32_t pclk_hz;
  uint16_t h_front, h_pulse, h_back;
  uint16_t v_front, v_pulse, v_back;
};

#ifdef BOARD_CROWPANEL_7
// 7.0" (EK9716BD3 + EK73002ACGB, TN): B3..B7, G2..G7, R3..R7 of the panel.
static constexpr RgbPins kPins = {
    {15, 7, 6, 5, 4}, {9, 46, 3, 8, 16, 1}, {14, 21, 47, 48, 45}, 41, 40, 39, 0};
// 24 MHz: about 49 frames per second, like Elecrow's V3.0 example. At 15 MHz
// (31 fps) the TN panel visibly flickers.
static constexpr RgbTiming kTiming = {24000000, 40, 48, 40, 1, 31, 13};
// V3.0 boards reset the touch controller through a PCA9557 I/O expander.
static constexpr uint8_t kExpanderAddr = 0x18;
#else
// 5.0"
static constexpr RgbPins kPins = {
    {8, 3, 46, 9, 1}, {5, 6, 7, 15, 16, 4}, {45, 48, 47, 21, 14}, 40, 41, 39, 0};
// 15 MHz: about 35 frames per second (12 MHz gave 28 and flickered).
static constexpr RgbTiming kTiming = {15000000, 8, 4, 43, 8, 4, 12};
#endif

// Set to true if taps land mirrored (top/bottom and left/right swapped):
// Elecrow says some 7.0" touch panels are mounted turned 180 degrees.
static constexpr bool kTouchRotated180 = false;

static constexpr int SCREEN_W = 800;
static constexpr int SCREEN_H = 480;
// One draw buffer of 24 lines (38 KB) in internal RAM. flush_cb copies it
// into the frame before returning, so a second buffer would never be drawn
// into while the first is flushed.
static constexpr int BUF_LINES = 24;
// The LCD reads two bounce buffers of 10 lines in internal RAM (2 x 16 KB),
// refilled from the frame in PSRAM, as Espressif recommends against drift.
static constexpr int BOUNCE_LINES = 10;

// ---- Code ----
// The picture: ESP-IDF's RGB panel driver (frame in PSRAM, bounce buffers).
// The program runs from PSRAM (CONFIG_SPIRAM_XIP_FROM_PSRAM), so writing to
// flash no longer pauses the picture. Touch and backlight via LovyanGFX.
static esp_lcd_panel_handle_t panel = nullptr;
static lgfx::Light_PWM light;
static lgfx::Touch_GT911 touch;

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
  // Copies into the frame (and writes the cache back) before it returns.
  esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px);
  lv_disp_flush_ready(drv);
}

static void touch_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
  lgfx::touch_point_t tp;
  if (touch.getTouchRaw(&tp, 1)) {
    int x = std::min<int>(std::max<int>(tp.x, 0), SCREEN_W - 1);
    int y = std::min<int>(std::max<int>(tp.y, 0), SCREEN_H - 1);
    if (kTouchRotated180) {
      x = SCREEN_W - 1 - x;
      y = SCREEN_H - 1 - y;
    }
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

static lv_color_t *alloc_buf(size_t px) {
  // Internal RAM is faster; fall back to PSRAM if Wi-Fi/TLS has taken it.
  void *p = heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  return static_cast<lv_color_t *>(p);
}

#ifdef BOARD_CROWPANEL_7
// Elecrow's V3.0 start-up (PCA9557 library: reset(), setMode(IO_OUTPUT), ...):
// hold the touch controller in reset with its INT line low (expander IO0 =
// reset, IO1 = INT), release reset, then let INT go.
// Without it the GT911 may not answer. Older boards have no expander and
// simply ignore these writes.
static void reset_touch_via_expander() {
  enum : uint8_t { REG_OUT = 1, REG_CFG = 3 };
  constexpr uint32_t kFreq = 400000;
  lgfx::i2c::init(I2C_PORT, I2C_SDA, I2C_SCL);
  auto w = [&](uint8_t reg, uint8_t v) {
    return lgfx::i2c::writeRegister8(I2C_PORT, kExpanderAddr, reg, v, 0, kFreq).has_value();
  };
  // IO2..IO7 must stay HIGH: the expander also drives the LCD's power and
  // reset lines, and holding them low leaves the panel showing only lines.
  if (!w(REG_OUT, 0xFF)) return;  // no expander on this board
  w(REG_CFG, 0x00);  // all outputs, all high
  w(REG_OUT, 0xFC);  // IO0 (touch reset) and IO1 (touch INT) low
  delay(20);
  w(REG_OUT, 0xFD);  // release touch reset
  delay(100);
  w(REG_CFG, 0x02);  // IO1 (touch INT) back to input
}
#endif

static void start_panel() {
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = kTiming.pclk_hz;
  cfg.timings.h_res = SCREEN_W;
  cfg.timings.v_res = SCREEN_H;
  cfg.timings.hsync_pulse_width = kTiming.h_pulse;
  cfg.timings.hsync_back_porch = kTiming.h_back;
  cfg.timings.hsync_front_porch = kTiming.h_front;
  cfg.timings.vsync_pulse_width = kTiming.v_pulse;
  cfg.timings.vsync_back_porch = kTiming.v_back;
  cfg.timings.vsync_front_porch = kTiming.v_front;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 1;
  cfg.bounce_buffer_size_px = SCREEN_W * BOUNCE_LINES;
  cfg.hsync_gpio_num = kPins.hsync;
  cfg.vsync_gpio_num = kPins.vsync;
  cfg.de_gpio_num = kPins.de;
  cfg.pclk_gpio_num = kPins.pclk;
  cfg.disp_gpio_num = -1;
  // RGB565, lowest bit first: B, then G, then R.
  int i = 0;
  for (int8_t p : kPins.b) cfg.data_gpio_nums[i++] = p;
  for (int8_t p : kPins.g) cfg.data_gpio_nums[i++] = p;
  for (int8_t p : kPins.r) cfg.data_gpio_nums[i++] = p;
  cfg.flags.fb_in_psram = 1;
  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
}

static void start_touch_and_light() {
  {
    auto cfg = light.config();
    cfg.pin_bl = GPIO_NUM_2;
    cfg.invert = false;
    cfg.freq = 44100;
    cfg.pwm_channel = 7;
    light.config(cfg);
    light.init(0);
  }
  {
    auto cfg = touch.config();
    cfg.x_min = 0;
    cfg.x_max = SCREEN_W - 1;
    cfg.y_min = 0;
    cfg.y_max = SCREEN_H - 1;
    cfg.pin_int = -1;
    cfg.pin_rst = -1;
    cfg.bus_shared = false;
    cfg.i2c_port = I2C_PORT;
    cfg.pin_sda = I2C_SDA;
    cfg.pin_scl = I2C_SCL;
    cfg.freq = 400000;
    cfg.i2c_addr = 0x14;  // the driver retries 0x5D if this one does not answer
    touch.config(cfg);
    touch.init();
  }
}

void display_init() {
  // Elecrow's examples hold GPIO38 low on the 5" and 7" boards before starting touch.
  pinMode(38, OUTPUT);
  digitalWrite(38, LOW);
#ifdef BOARD_CROWPANEL_7
  reset_touch_via_expander();
#endif

  // Set up on this core (the one running LVGL), as Espressif recommends.
  start_panel();
  start_touch_and_light();

  lv_init();
  const size_t px = SCREEN_W * BUF_LINES;
  lv_disp_draw_buf_init(&draw_buf, alloc_buf(px), nullptr, px);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_cb;
  lv_indev_drv_register(&indev_drv);

  light.setBrightness(200);
}

static uint8_t brightness = 200;
static bool blanked = false;

void display_set_brightness(uint8_t level) {
  brightness = level;
  if (!blanked) light.setBrightness(level);
}

void display_blank(bool blank) {
  blanked = blank;
  light.setBrightness(blank ? 0 : brightness);
}
