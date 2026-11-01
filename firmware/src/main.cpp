// Calendar and to-do display for the Elecrow CrowPanel 7.0" or 5.0" (ESP32-S3).
// Core 1 runs only the UI (this loop), so touch and page changes never wait on
// the network. Wi-Fi, sync, the setup page and OTA run in their own tasks on
// core 0 and hand data over through sync_get_if_newer().
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "audio.h"
#include "battery.h"
#include "desk.h"
#include "display.h"
#include "harness.h"
#include "ota.h"
#include "sensors.h"
#include "settings.h"
#include "storage.h"
#include "sync.h"
#include "ui.h"
#include "web.h"
#include "wifi_mgr.h"

static Snapshot snap;
static uint32_t lastTick;

time_t ui_now() { return time(nullptr); }

void ui_set_backlight(uint8_t percent) {
  if (percent > 100) percent = 100;
  display_set_brightness((uint16_t)percent * 255 / 100);
}

void setup() {
  // malloc() keeps blocks up to 4 KB in internal RAM by default. The calendar
  // and task data (many small strings, JSON) filled it until Wi-Fi and HTTPS
  // ran short (the 7" went down to 1.4 KB free). Only blocks under 512 bytes
  // stay internal now; the rest goes to PSRAM.
  heap_caps_malloc_extmem_enable(512);
  Serial.begin(115200);
  storage_begin();  // first, so settings can fall back to the SD backup
  settings_begin();
  display_init();
  sensors_begin();  // after the display: the light sensor uses the touch I2C bus
  battery_begin();
  audio_begin();
  wifi_begin();
  ui_init();
  harness_begin();
  sync_start();
  web_begin();
  ota_begin();
  desk_begin();
}

void loop() {
  if (sync_get_if_newer(snap.version, snap)) ui_update(snap);
  if (millis() - lastTick >= 200) {
    lastTick = millis();
    ui_tick();
  }
  settings_flush();
  harness_poll();
  const uint32_t idle = lv_timer_handler();
  delay(idle < 5 ? idle : 5);  // yield, but never longer than LVGL asks for
}
