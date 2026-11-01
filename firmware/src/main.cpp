// Calendar and to-do display for the Elecrow CrowPanel 7.0" or 5.0" (ESP32-S3).
// Core 1 runs only the UI (this loop), so touch and page changes never wait on
// the network. Wi-Fi, sync, the setup page and OTA run in their own tasks on
// core 0 and hand data over through sync_get_if_newer().
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "audio.h"
#include "battery.h"
#include "crashlog.h"
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

// malloc(), new and String go to PSRAM first, whatever their size (ESP-IDF
// keeps blocks under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL, 4 KB, in internal
// RAM). The calendar's thousands of small strings, copied for the UI and the
// sync, used up the internal RAM that Wi-Fi, TLS, Bluetooth and the screen
// need. Those ask for internal RAM explicitly, so they are not affected; and
// malloc() still falls back to internal RAM if PSRAM is ever full.
static constexpr size_t kInternalMallocMax = 0;

static uint32_t snapVersion;
static uint32_t lastTick;

time_t ui_now() { return time(nullptr); }

void ui_set_backlight(uint8_t percent) {
  if (percent > 100) percent = 100;
  display_set_brightness((uint16_t)percent * 255 / 100);
}

void setup() {
  heap_caps_malloc_extmem_enable(kInternalMallocMax);
  Serial.begin(115200);
  crashlog_begin();  // prints why the display restarted
  storage_begin();  // first, so settings can fall back to the SD backup
  settings_begin();
  display_init();
  sensors_begin();  // after the display: the light sensor uses the touch I2C bus
  battery_begin();
  audio_begin();
  wifi_begin();
  desk_begin();  // before the UI: Home's desk dock reads the desk status while it is built
  ui_init();
  harness_begin();
  sync_start();
  web_begin();
  ota_begin();
}

void loop() {
  {
    // The UI keeps its own copy (g_snap); this one only lives for the hand-over.
    Snapshot snap;
    if (sync_get_if_newer(snapVersion, snap)) {
      snapVersion = snap.version;
      ui_update(snap);
    }
  }
  if (millis() - lastTick >= 200) {
    lastTick = millis();
    ui_tick();
  }
  settings_flush();
  harness_poll();
  crashlog_poll();
  const uint32_t idle = lv_timer_handler();
  delay(idle < 5 ? idle : 5);  // yield, but never longer than LVGL asks for
}
