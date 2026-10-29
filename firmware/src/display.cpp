#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lvgl.h>

#include "display.h"

// Pin map and timings for the CrowPanel 5.0" (from Elecrow's reference sketches).
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB bus;
  lgfx::Panel_RGB panel;
  lgfx::Light_PWM light;
  lgfx::Touch_GT911 touch;

  LGFX() {
    {
      auto cfg = panel.config();
      cfg.memory_width = 800;
      cfg.memory_height = 480;
      cfg.panel_width = 800;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      panel.config(cfg);
    }
    {
      auto cfg = panel.config_detail();
      cfg.use_psram = 1;
      panel.config_detail(cfg);
    }
    {
      auto cfg = bus.config();
      cfg.panel = &panel;
      cfg.pin_d0 = GPIO_NUM_8;    // B0
      cfg.pin_d1 = GPIO_NUM_3;    // B1
      cfg.pin_d2 = GPIO_NUM_46;   // B2
      cfg.pin_d3 = GPIO_NUM_9;    // B3
      cfg.pin_d4 = GPIO_NUM_1;    // B4
      cfg.pin_d5 = GPIO_NUM_5;    // G0
      cfg.pin_d6 = GPIO_NUM_6;    // G1
      cfg.pin_d7 = GPIO_NUM_7;    // G2
      cfg.pin_d8 = GPIO_NUM_15;   // G3
      cfg.pin_d9 = GPIO_NUM_16;   // G4
      cfg.pin_d10 = GPIO_NUM_4;   // G5
      cfg.pin_d11 = GPIO_NUM_45;  // R0
      cfg.pin_d12 = GPIO_NUM_48;  // R1
      cfg.pin_d13 = GPIO_NUM_47;  // R2
      cfg.pin_d14 = GPIO_NUM_21;  // R3
      cfg.pin_d15 = GPIO_NUM_14;  // R4
      cfg.pin_henable = GPIO_NUM_40;
      cfg.pin_vsync = GPIO_NUM_41;
      cfg.pin_hsync = GPIO_NUM_39;
      cfg.pin_pclk = GPIO_NUM_0;
      cfg.freq_write = 12000000;  // 12 MHz leaves PSRAM bandwidth for Wi-Fi without flicker
      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch = 43;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 8;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch = 12;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;
      bus.config(cfg);
    }
    panel.setBus(&bus);
    {
      auto cfg = light.config();
      cfg.pin_bl = GPIO_NUM_2;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      light.config(cfg);
    }
    panel.light(&light);
    {
      auto cfg = touch.config();
      cfg.x_min = 0;
      cfg.x_max = 799;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.pin_int = -1;
      cfg.pin_rst = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = I2C_NUM_1;
      cfg.pin_sda = GPIO_NUM_19;
      cfg.pin_scl = GPIO_NUM_20;
      cfg.freq = 400000;
      cfg.i2c_addr = 0x14;  // the driver retries 0x5D if this one does not answer
      touch.config(cfg);
    }
    panel.setTouch(&touch);
    setPanel(&panel);
  }
};

static LGFX lcd;

static constexpr int SCREEN_W = 800;
static constexpr int SCREEN_H = 480;
static constexpr int BUF_LINES = 48;

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  lcd.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&px->full);
  lv_disp_flush_ready(drv);
}

static void touch_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
  uint16_t x, y;
  if (lcd.getTouch(&x, &y)) {
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

void display_init() {
  // Elecrow's examples hold GPIO38 low on the 5" and 7" boards before starting touch.
  pinMode(38, OUTPUT);
  digitalWrite(38, LOW);

  lcd.begin();
  lcd.fillScreen(TFT_BLACK);
  lcd.setBrightness(200);

  lv_init();
  const size_t px = SCREEN_W * BUF_LINES;
  lv_disp_draw_buf_init(&draw_buf, alloc_buf(px), alloc_buf(px), px);

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
}

void display_set_brightness(uint8_t level) {
  lcd.setBrightness(level);
}
