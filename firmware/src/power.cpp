// Low power: see power.h. How a tap wakes the board:
//
// - Light sleep, TOUCH_INT_GPIO = -1 (both boards for now): the chip wakes on
//   a timer every kLightPollMs, reads the GT911 over I2C and sleeps again unless
//   it is touched. LVGL does not run meanwhile. Tap latency is about that interval.
// - Light sleep with TOUCH_INT_GPIO set: the INT line (low on touch) also wakes
//   it at once, and the timer only backs it up (kPinPollMs).
// - Deep sleep, no RTC wake pin: the board boots every kDeepPollS, and
//   power_begin() reads the touch before the display starts. So a tap takes
//   up to about 1 s plus a full boot (2-3 s) to show the screen, and every check
//   costs a short boot (the program is copied to PSRAM each time), which eats
//   into the saving. Light sleep is the better choice on these boards.
// - Deep sleep with TOUCH_INT_GPIO an RTC GPIO (0-21): ext1 wakes on the pin.
// A timer at the window's end is always armed as well, so the board never
// sleeps for good.
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <lvgl.h>

#include "config.h"
#include "display.h"
#include "ota.h"
#include "power.h"
#include "presence.h"
#include "settings.h"
#include "sync.h"
#include "wifi_mgr.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr uint32_t kCheckMs = 5000;         // how often loop() looks at the window
constexpr uint32_t kLightPollMs = 100;      // light sleep: ask the touch controller this often
constexpr uint32_t kPinPollMs = 1000;       // ...when the INT pin can wake it too
constexpr uint32_t kDeepPollS = 1;          // deep sleep without a wake pin: boot this often
constexpr time_t kMaxSleepS = 3 * 86400;    // never arm a longer sleep than this
constexpr time_t kClockValid = 1700000000;  // the clock has been set once it is past this
constexpr uint32_t kRtcMagic = 0x534C4550;  // "SLEP": a deep sleep is going on

// ---- State -------------------------------------------------------------------

// RTC memory survives deep sleep. Checked against the reset reason and kRtcMagic.
RTC_NOINIT_ATTR uint32_t rtcMagic;
RTC_NOINIT_ATTR time_t rtcWakeAt;  // when the deep sleep ends (window end)
RTC_NOINIT_ATTR char rtcTz[32];    // the time zone; a deep sleep forgets TZ

uint32_t lastCheck = 0;
bool parking = false;    // waiting for the sync worker to stop
time_t forcedUntil = 0;  // harness "sleep"

// ---- Code --------------------------------------------------------------------

const char *causeName(esp_sleep_wakeup_cause_t c) {
  switch (c) {
    case ESP_SLEEP_WAKEUP_TIMER: return "timer";
    case ESP_SLEEP_WAKEUP_GPIO: return "touch INT pin (gpio)";
    case ESP_SLEEP_WAKEUP_EXT1: return "touch INT pin (ext1)";
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "not a sleep wake (power on or reset)";
    default: return "other";
  }
}

bool inWindow(const Settings &s, time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  if (s.sleepWeekends && (tm.tm_wday == 0 || tm.tm_wday == 6)) return true;
  const int m = tm.tm_hour * 60 + tm.tm_min, a = s.sleepStart, b = s.sleepEnd;
  if (a == b) return false;
  return a < b ? (m >= a && m < b) : (m >= a || m < b);
}

// The first minute after `now` that is outside the window.
time_t windowEnd(const Settings &s, time_t now) {
  time_t t = now - now % 60;
  while (t < now + kMaxSleepS && inWindow(s, t)) t += 60;
  return t;
}

// Arms the wake sources for one deep sleep, ending at the latest in `left` seconds.
void armDeep(time_t left) {
  left = left < 1 ? 1 : left > kMaxSleepS ? kMaxSleepS : left;
  bool pin = false;
#if TOUCH_INT_GPIO >= 0 && TOUCH_INT_GPIO <= 21
  // Only while INT is idle (high): a level that is already low would wake it at once, forever.
  if (gpio_get_level((gpio_num_t)TOUCH_INT_GPIO)) {
    esp_sleep_enable_ext1_wakeup_io(1ULL << TOUCH_INT_GPIO, ESP_EXT1_WAKEUP_ANY_LOW);
    pin = true;
  }
#endif
  esp_sleep_enable_timer_wakeup((uint64_t)(pin || left < (time_t)kDeepPollS ? left : kDeepPollS) * 1000000ULL);
}

[[noreturn]] void deepSleep(time_t until) {
  const char *tz = getenv("TZ");
  strlcpy(rtcTz, tz ? tz : "", sizeof(rtcTz));
  rtcWakeAt = until;
  rtcMagic = kRtcMagic;
  armDeep(until - time(nullptr));
  display_park();
  Serial.flush();
  esp_deep_sleep_start();
}

// Light sleep until `until` or a tap. Returns what ended it.
const char *lightSleep(time_t until) {
  // Bluetooth (phone presence) does not survive a forced light sleep: then
  // only the screen is off and the CPU naps with delay().
  const bool cpuSleep = !presence_ready();
  if (!cpuSleep) Serial.println("[power] Bluetooth is on: screen off only, no CPU sleep");
  uint32_t pollMs = kLightPollMs;
#if TOUCH_INT_GPIO >= 0
  const gpio_num_t pin = (gpio_num_t)TOUCH_INT_GPIO;
  gpio_set_direction(pin, GPIO_MODE_INPUT);
  if (cpuSleep && gpio_get_level(pin)) {  // idle high, else a level wake would fire at once
    gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    pollMs = kPinPollMs;
  }
#endif
  Serial.flush();
  display_touched();  // clear an old report, so it does not count as a tap
  const char *why = "window end";
  while (time(nullptr) < until) {
    if (cpuSleep) {
      esp_sleep_enable_timer_wakeup(pollMs * 1000ULL);
      esp_light_sleep_start();
    } else {
      delay(pollMs);
    }
    if (display_touched()) {
      why = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO ? "touch (INT pin)" : "touch";
      break;
    }
    delay(1);  // let this core's idle task run (task watchdog)
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
#if TOUCH_INT_GPIO >= 0
  gpio_wakeup_disable(pin);
#endif
  return why;
}

void sleepUntil(time_t until, bool deep) {
  const time_t now = time(nullptr);
  if (until - now > kMaxSleepS) until = now + kMaxSleepS;
  Serial.printf("[power] %s sleep for %ld s\n", deep ? "deep" : "light", (long)(until - now));
  settings_flush(true);  // never sleep (or restart) with settings not yet in flash
  display_sleep(true);
  wifi_sleep(true);
  if (deep) deepSleep(until);
  const char *why = lightSleep(until);
  Serial.printf("[power] awake after %ld s, woken by %s\n", (long)(time(nullptr) - now), why);
  wifi_sleep(false);
  if (!display_sleep(false)) {
    Serial.println("[power] panel did not restart, restarting the board");
    esp_restart();
  }
  lv_disp_trig_activity(nullptr);  // the waking tap counts as a touch
  sync_pause(false);
  sync_refresh();
}

}  // namespace

void power_begin() {
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
    rtcMagic = 0;
    Serial.printf("[power] wake cause: %s\n", causeName(cause));
    return;
  }
  rtcTz[sizeof(rtcTz) - 1] = 0;
  if (rtcTz[0]) {
    setenv("TZ", rtcTz, 1);
    tzset();
  }
  if (rtcMagic != kRtcMagic) return;
  // Still in the window and nobody touched it: back to sleep before the display starts.
  const time_t now = time(nullptr);
  const bool sleeping = now < rtcWakeAt && rtcWakeAt - now <= kMaxSleepS;
  if (sleeping && !display_touched()) {
    armDeep(rtcWakeAt - now);
    display_park();
    esp_deep_sleep_start();
  }
  rtcMagic = 0;
  Serial.printf("[power] wake cause: %s, %s\n", causeName(cause), sleeping ? "touched" : "sleep over");
}

void power_poll() {
  if (millis() - lastCheck < kCheckMs && !forcedUntil) return;
  lastCheck = millis();
  const Settings s = settings_get();
  const time_t now = time(nullptr);
  const OtaState ota = ota_status().state;
  const bool want = forcedUntil ||
                    (s.lowPower && now > kClockValid && inWindow(s, now) &&
                     lv_disp_get_inactive_time(nullptr) >= s.sleepIdleMinutes * 60000UL && ota != OtaState::Checking &&
                     ota != OtaState::Installing && ota != OtaState::Done);
  if (!want) {
    if (parking) sync_pause(false);
    parking = false;
    return;
  }
  parking = true;
  if (!sync_pause(true)) return;  // not between two rounds yet; ask again next time
  parking = false;
  const time_t until = forcedUntil ? forcedUntil : windowEnd(s, now);
  forcedUntil = 0;
  sleepUntil(until, s.sleepMode == 1);
}

void power_sleep_for(uint32_t seconds) {
  forcedUntil = time(nullptr) + (seconds ? seconds : 1);
}
