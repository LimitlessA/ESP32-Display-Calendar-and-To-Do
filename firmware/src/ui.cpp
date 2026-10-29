#include <map>
#include <set>
#include <vector>

#include "audio.h"
#include "ota.h"
#include "ui.h"
#include "ui_internal.h"
#include "wifi_mgr.h"

Snapshot g_snap;
Settings g_settings;

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr uint32_t kToastMs = 4500;        // how long a message stays on screen
constexpr uint32_t kToastActionMs = 7000;  // ...when it has a button such as Undo
constexpr uint32_t kAutoFirstCheckMs = 60000;  // first firmware check one minute after start
constexpr uint32_t kNightIdleMs = 30000;  // back to the night clock after a tap
constexpr uint8_t kNightBacklight = 6;
constexpr uint8_t kDimBacklight = 20;
constexpr uint32_t kAutoCheckMs = 3 * 3600000UL;  // look for new firmware every 3 hours
constexpr uint32_t kAutoIdleMs = 15 * 60000UL;    // ...and install it after 15 min without touch
constexpr time_t kAutoMeetingGap = 15 * 60;        // but not right before a meeting

// ---- State -------------------------------------------------------------------

lv_obj_t *screens[(int)Screen::Count];
bool dirty[(int)Screen::Count];
Screen current = Screen::Home;
Screen beforeNight = Screen::Home;

struct ClockLabels {
  lv_obj_t *time;
  lv_obj_t *date;
};
std::vector<ClockLabels> clocks;
int lastMinute = -1;

std::set<String> reminded;
std::map<String, time_t> snoozedUntil;
std::map<String, int> reminderOverride;

bool dimmed = false;
uint32_t lastAutoCheck = 0;
uint8_t appliedTheme = 255;
uint32_t seenSettings = 0;

lv_obj_t *toastBox = nullptr;
lv_timer_t *toastTimer = nullptr;

// ---- Code --------------------------------------------------------------------

void refresh(Screen s) {
  dirty[(int)s] = false;
  switch (s) {
    case Screen::Home: home_refresh(); break;
    case Screen::Week: week_refresh(); break;
    case Screen::Tasks: tasks_refresh(); break;
    case Screen::Night: night_refresh(); break;
    case Screen::Accounts: accounts_refresh(); break;
    case Screen::Settings: settings_refresh(); break;
    default: break;
  }
}

void updateClocks() {
  const String time = fmt_clock();
  const String date = clock_valid() ? fmt_date_short(ui_now()) : String("");
  for (const ClockLabels &c : clocks) {
    lv_label_set_text(c.time, time.c_str());
    if (c.date) lv_label_set_text(c.date, date.c_str());
  }
}

void buildAll() {
  theme_select(g_settings.theme);
  appliedTheme = g_settings.theme;

  lv_disp_t *disp = lv_disp_get_default();
  lv_theme_t *th = lv_theme_default_init(disp, c(theme().accent), c(theme().good), theme().dark, &font_16);
  lv_disp_set_theme(disp, th);

  lv_obj_t *old[(int)Screen::Count];
  memcpy(old, screens, sizeof(old));
  clocks.clear();
  reminder_hide();

  screens[(int)Screen::Home] = home_create();
  screens[(int)Screen::Detail] = detail_create();
  screens[(int)Screen::Week] = week_create();
  screens[(int)Screen::Tasks] = tasks_create();
  screens[(int)Screen::Night] = night_create();
  screens[(int)Screen::Wifi] = wifi_create();
  screens[(int)Screen::Accounts] = accounts_create();
  screens[(int)Screen::Settings] = settings_create();
  for (bool &d : dirty) d = true;

  if (current == Screen::Detail) current = Screen::Home;  // its event is gone
  refresh(current);
  lv_scr_load(screens[(int)current]);
  for (lv_obj_t *o : old) {
    if (o) lv_obj_del(o);
  }
  updateClocks();
}

bool inNightWindow() {
  if (!g_settings.nightMode || !clock_valid()) return false;
  struct tm tm;
  const time_t now = ui_now();
  localtime_r(&now, &tm);
  const int m = tm.tm_hour * 60 + tm.tm_min;
  const int a = g_settings.nightStart, b = g_settings.nightEnd;
  return a <= b ? (m >= a && m < b) : (m >= a || m < b);
}

void checkReminders() {
  if (!clock_valid() || reminder_visible()) return;
  const time_t now = ui_now();
  for (const Event &ev : g_snap.events) {
    if (ev.allDay) continue;
    const time_t start = ev.start / 1000;
    if (start > now + g_settings.remindMinutes * 60 || start < now - 60) continue;
    const int override = ui_reminder_override(ev.key);
    if (override == 0 || (override < 0 && !g_settings.chime)) continue;
    auto snooze = snoozedUntil.find(ev.key);
    if (snooze != snoozedUntil.end()) {
      if (now < snooze->second) continue;
      snoozedUntil.erase(snooze);
    } else if (reminded.count(ev.key)) {
      continue;
    }
    reminded.insert(ev.key);
    if (current == Screen::Night) ui_show(beforeNight);
    ui_set_backlight(g_settings.brightness);
    dimmed = false;
    reminder_show(ev);
    audio_chime(g_settings.volume);
    return;
  }
}

void checkNightAndDim() {
  const uint32_t idle = lv_disp_get_inactive_time(nullptr);
  const bool night = inNightWindow();

  if (night && current != Screen::Night && idle > kNightIdleMs && !reminder_visible()) {
    beforeNight = Screen::Home;
    ui_show(Screen::Night);
  } else if (!night && current == Screen::Night) {
    ui_show(Screen::Home);
  }
  if (current == Screen::Night) return;

  if (g_settings.dimIdle && !dimmed && idle > g_settings.dimAfterMinutes * 60000UL) {
    ui_set_backlight(kDimBacklight);
    dimmed = true;
  } else if (dimmed && idle < 1000) {
    ui_set_backlight(g_settings.brightness);
    dimmed = false;
  }
}

// Automatic updates: check now and then, and install when nobody is using the
// display (at night, or after a long time without touch) and no meeting is close.
void autoUpdate() {
  if (!g_settings.autoUpdate || wifi_state() != WifiState::Connected) return;
  const OtaStatus st = ota_status();
  if (st.state == OtaState::Checking || st.state == OtaState::Installing || st.state == OtaState::Done) return;
  const uint32_t ms = millis();
  if (ms > kAutoFirstCheckMs && (lastAutoCheck == 0 || ms - lastAutoCheck > kAutoCheckMs)) {
    lastAutoCheck = ms;
    ota_check();
    return;
  }
  if (st.state != OtaState::Available || reminder_visible()) return;
  if (!inNightWindow() && lv_disp_get_inactive_time(nullptr) < kAutoIdleMs) return;
  const time_t now = ui_now();
  for (const Event &e : g_snap.events) {
    const time_t start = e.start / 1000;
    if (!e.allDay && start > now && start - now < kAutoMeetingGap) return;
  }
  ota_install();  // the update dialog opens by itself and shows progress
}

void hideToast(lv_timer_t *) {
  if (toastBox) lv_obj_del(toastBox);
  toastBox = nullptr;
  if (toastTimer) lv_timer_del(toastTimer);
  toastTimer = nullptr;
}

}  // namespace

// ---- Public UI API ---------------------------------------------------------

void ui_init() {
  seenSettings = settings_version();
  g_settings = settings_get();
  ui_set_backlight(g_settings.brightness);
  if (!g_settings.wifiSsid.length()) current = Screen::Wifi;  // first start: connect first
  buildAll();
  if (current == Screen::Wifi) wifi_on_show();  // start the network scan
}

void ui_update(const Snapshot &snap) {
  const bool hadError = g_snap.error.length() > 0;
  g_snap = snap;
  for (bool &d : dirty) d = true;
  refresh(current);
  // Surface new problems once; the Settings screen keeps showing them.
  if (snap.error.length() && !hadError && current != Screen::Night) toast(snap.error);
}

void ui_tick() {
  // Settings can also change from the setup page or the Wi-Fi task.
  if (settings_version() != seenSettings) {
    seenSettings = settings_version();
    const Settings s = settings_get();
    if (s.theme != appliedTheme) {
      g_settings = s;
      buildAll();
    } else {
      g_settings = s;
    }
  }

  const time_t now = ui_now();
  const int minute = clock_valid() ? (int)(now / 60) : -1;
  if (minute != lastMinute) {
    lastMinute = minute;
    updateClocks();
    // Relative times ("in 5 min") and day boundaries move with the clock.
    for (bool &d : dirty) d = true;
    refresh(current);
  }
  if (current == Screen::Detail) detail_tick();
  if (current == Screen::Wifi) wifi_tick();
  ota_dialog_tick();
  checkReminders();
  checkNightAndDim();
  autoUpdate();
}

// ---- Navigation and shared helpers ----------------------------------------

void ui_show(Screen s) {
  if (s == current && lv_scr_act() == screens[(int)s]) return;
  if (s == Screen::Night) {
    beforeNight = current == Screen::Night ? beforeNight : current;
    ui_set_backlight(kNightBacklight);
  } else if (current == Screen::Night) {
    ui_set_backlight(g_settings.brightness);
  }
  current = s;
  if (dirty[(int)s]) refresh(s);
  if (s == Screen::Week) week_on_show();
  if (s == Screen::Wifi) wifi_on_show();
  lv_scr_load(screens[(int)s]);
}

Screen ui_current() {
  return current;
}

void ui_open_event(const Event &ev) {
  detail_show(ev);
  ui_show(Screen::Detail);
}

void ui_save_settings(const Settings &s) {
  const bool themeChanged = s.theme != g_settings.theme;
  const bool brightnessChanged = s.brightness != g_settings.brightness;
  g_settings = s;
  settings_save(s);
  if (brightnessChanged) ui_set_backlight(s.brightness);
  if (themeChanged) buildAll();
}

int ui_reminder_override(const String &key) {
  auto it = reminderOverride.find(key);
  return it == reminderOverride.end() ? -1 : it->second;
}

void ui_set_reminder_override(const String &key, int value) {
  reminderOverride[key] = value;
  reminded.erase(key);
}

void ui_snooze_reminder(const String &key, int minutes) {
  snoozedUntil[key] = ui_now() + minutes * 60;
}

void register_clock(lv_obj_t *time_label, lv_obj_t *date_label) {
  clocks.push_back({time_label, date_label});
}

void toast(const String &msg, const char *action, lv_event_cb_t cb, void *user) {
  hideToast(nullptr);
  const Theme &t = theme();
  toastBox = mk_box(lv_layer_top(), t.raised, 14);
  lv_obj_set_style_border_color(toastBox, c(t.line), 0);
  lv_obj_set_style_border_width(toastBox, 1, 0);
  lv_obj_set_style_pad_left(toastBox, 18, 0);
  lv_obj_set_style_pad_right(toastBox, action ? 6 : 18, 0);
  lv_obj_set_style_pad_ver(toastBox, action ? 6 : 14, 0);
  lv_obj_set_style_max_width(toastBox, SCREEN_W - 80, 0);
  set_flex(toastBox, LV_FLEX_FLOW_ROW, 14);
  lv_obj_t *l = mk_label(toastBox, &font_14, t.text, msg.c_str());
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(l, SCREEN_W - 260, 0);
  if (action) {
    lv_obj_t *b = mk_text_button(toastBox, action, true);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_add_event_cb(b, [](lv_event_t *) { lv_async_call([](void *) { hideToast(nullptr); }, nullptr); },
                        LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_align(toastBox, LV_ALIGN_BOTTOM_MID, 0, -14);
  toastTimer = lv_timer_create(hideToast, action ? kToastActionMs : kToastMs, nullptr);
}
