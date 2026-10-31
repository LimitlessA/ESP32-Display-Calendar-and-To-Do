#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
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
static constexpr RgbTiming kTiming = {15000000, 40, 48, 40, 1, 31, 13};
// V3.0 boards reset the touch controller through a PCA9557 I/O expander.
static constexpr uint8_t kExpanderAddr = 0x18;
#else
// 5.0"
static constexpr RgbPins kPins = {
    {8, 3, 46, 9, 1}, {5, 6, 7, 15, 16, 4}, {45, 48, 47, 21, 14}, 40, 41, 39, 0};
// 12 MHz leaves PSRAM bandwidth for Wi-Fi without flicker
static constexpr RgbTiming kTiming = {12000000, 8, 4, 43, 8, 4, 12};
#endif

// Set to true if taps land mirrored (top/bottom and left/right swapped):
// Elecrow says some 7.0" touch panels are mounted turned 180 degrees.
static constexpr bool kTouchRotated180 = false;

// ---- Code ----
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
      cfg.pin_d0 = kPins.b[0];
      cfg.pin_d1 = kPins.b[1];
      cfg.pin_d2 = kPins.b[2];
      cfg.pin_d3 = kPins.b[3];
      cfg.pin_d4 = kPins.b[4];
      cfg.pin_d5 = kPins.g[0];
      cfg.pin_d6 = kPins.g[1];
      cfg.pin_d7 = kPins.g[2];
      cfg.pin_d8 = kPins.g[3];
      cfg.pin_d9 = kPins.g[4];
      cfg.pin_d10 = kPins.g[5];
      cfg.pin_d11 = kPins.r[0];
      cfg.pin_d12 = kPins.r[1];
      cfg.pin_d13 = kPins.r[2];
      cfg.pin_d14 = kPins.r[3];
      cfg.pin_d15 = kPins.r[4];
      cfg.pin_henable = kPins.de;
      cfg.pin_vsync = kPins.vsync;
      cfg.pin_hsync = kPins.hsync;
      cfg.pin_pclk = kPins.pclk;
      cfg.freq_write = kTiming.pclk_hz;
      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = kTiming.h_front;
      cfg.hsync_pulse_width = kTiming.h_pulse;
      cfg.hsync_back_porch = kTiming.h_back;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = kTiming.v_front;
      cfg.vsync_pulse_width = kTiming.v_pulse;
      cfg.vsync_back_porch = kTiming.v_back;
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
      cfg.offset_rotation = kTouchRotated180 ? 2 : 0;
      cfg.i2c_port = I2C_PORT;
      cfg.pin_sda = I2C_SDA;
      cfg.pin_scl = I2C_SCL;
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
// Two draw buffers of 24 lines (2 x 38 KB) in internal RAM. 48 lines took
// 154 KB and, with Wi-Fi, left too little for a scan to start.
static constexpr int BUF_LINES = 24;

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

void display_init() {
  // Elecrow's examples hold GPIO38 low on the 5" and 7" boards before starting touch.
  pinMode(38, OUTPUT);
  digitalWrite(38, LOW);
#ifdef BOARD_CROWPANEL_7
  reset_touch_via_expander();
#endif

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
