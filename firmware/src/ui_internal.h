// Shared pieces of the LVGL user interface. Each screen lives in its own
// ui_<screen>.cpp and is built once; lists are rebuilt only when their data
// changes and only while the screen is visible, so screen changes are instant.
#pragma once

#include <lvgl.h>
#include <vector>

#include "icons.h"
#include "model.h"
#include "settings.h"
#include "theme.h"
#include "ui.h"

LV_FONT_DECLARE(font_12);
LV_FONT_DECLARE(font_14);
LV_FONT_DECLARE(font_14b);
LV_FONT_DECLARE(font_16);
LV_FONT_DECLARE(font_16s);
LV_FONT_DECLARE(font_20b);
LV_FONT_DECLARE(font_26b);
LV_FONT_DECLARE(font_34b);
LV_FONT_DECLARE(font_72);
LV_FONT_DECLARE(font_150);

constexpr int SCREEN_W = 800;
constexpr int SCREEN_H = 480;
constexpr int HEADER_H = 56;
constexpr int PAD = 14;
constexpr int GAP = 12;
constexpr int TOUCH = 44;  // smallest touch target

enum class Screen { Home, Detail, Week, Tasks, Night, Wifi, Accounts, Settings, Features, Dates, Desk, Count };

// Latest data and settings, owned by ui.cpp.
extern Snapshot g_snap;
extern Settings g_settings;

// Words used on several screens (defined at the top of ui_common.cpp).
extern const char *const kToday;
extern const char *const kTomorrow;
extern const char *const kAllDay;

// printf-style formatting into a String, for the text templates at the top of each file.
String strf(const char *fmt, ...);

// Provided by the platform (main.cpp on the device, the simulator on a PC).
time_t ui_now();

// Navigation
void ui_show(Screen s);
Screen ui_current();
void ui_open_event(const Event &ev);   // meeting details
void ui_save_settings(const Settings &s);  // saves and applies (theme, layouts, brightness)
bool overlay_visible();                    // a dialog or alert covers the screen

// Sounds, played only if Settings > Features > Reminder sound is on.
enum class Sound { Chime, Alert, Done };
void play(Sound s);

// Backlight level for daytime use: the brightness setting, lowered in a dark
// room when the ambient light feature is on.
uint8_t day_brightness();

// Per-event reminder override: -1 none, 0 off, 1 on.
int ui_reminder_override(const String &key);
void ui_set_reminder_override(const String &key, int value);
void ui_snooze_reminder(const String &key, int minutes);

// Screen builders and refreshers. create() builds the static parts once;
// refresh() rebuilds the data-driven parts.
lv_obj_t *home_create();
void home_refresh();
lv_obj_t *detail_create();
void detail_show(const Event &ev);
void detail_tick();
lv_obj_t *week_create();
void week_refresh();
void week_on_show();
lv_obj_t *tasks_create();
void tasks_refresh();
lv_obj_t *night_create();
void night_refresh();
lv_obj_t *wifi_create();
void wifi_on_show();
void wifi_tick();
lv_obj_t *accounts_create();
void accounts_refresh();
lv_obj_t *settings_create();
void settings_refresh();

lv_obj_t *features_create();
void features_refresh();
int features_on_count();
lv_obj_t *dates_create();
void dates_refresh();

// Overlays on the top layer
void reminder_show(const Event &ev);
void reminder_hide();
bool reminder_visible();
void ota_dialog_show();
void ota_dialog_tick();
void theme_picker_show();
void calendars_dialog_show(uint8_t account);
// Task check box (shared by Home and Tasks): ticks, completes, offers Undo.
lv_obj_t *mk_check(lv_obj_t *parent, const Task &task, int size);
void complete_with_undo(lv_obj_t *box, const Task &task);

void layout_picker_show();
void quick_add_show();
void summary_show();
void review_show();

// Focus timer (ui_focus.cpp)
bool focus_enabled_for(const Task &task);
void focus_start(const Task &task, int minutes = 0);  // 0 = the Focus timer setting
void focus_hold(lv_obj_t *row, const Task &task);  // holding the row starts the timer
bool focus_running();
void focus_stop();
// Header pill text, e.g. "18:04  Send invoice"; false when nothing to show there.
bool focus_pill(String &time, String &title, float &progress);
void focus_open();  // full-screen timer
void focus_tick();

// Meeting conflicts and leave-by alerts (ui_alerts.cpp)
struct Conflict {
  Event a, b;
  time_t from, to;  // the overlap
};
std::vector<Conflict> find_conflicts(const std::vector<Event> &events, time_t from, time_t to);
bool has_conflict(const Event &ev, const std::vector<Conflict> &all, String *other = nullptr);
void conflict_popup_show(const Conflict &cf);
lv_obj_t *mk_conflict_banner(lv_obj_t *parent, int w);  // Home, layout A; nullptr if none
// The next meeting you need to leave for, if its warning time is near.
const Event *leave_next(time_t &leaveAt);
lv_obj_t *mk_leave_card(lv_obj_t *parent, int w);      // Home, layout B; nullptr if none
String travel_text(const Event &ev);  // "25 min travel from home. Leave by 13:35.", "" if unknown
void alerts_tick();

// Countdowns, birthdays and holidays (ui_dates.cpp)
bool dates_any();
void mk_dates_column(lv_obj_t *parent, int w);  // Home, layout A
lv_obj_t *mk_dates_ribbon(lv_obj_t *parent, int w);  // Home, layout B; nullptr if empty
long days_until(const String &dayKey);
String day_key(time_t t);  // local yyyy-MM-dd

// Daily summary and end-of-day review (ui_daily.cpp)
void daily_tick();

// Meeting timer and meeting cost (ui_meeting.cpp)
const Event *meeting_now();                    // the timed meeting under way, if any
long meeting_cost(const Event &ev, time_t upTo);  // people x rate x hours, whole currency units
void mk_meeting_extras(lv_obj_t *card, const Event &ev, int w);  // Home card, layouts A
bool meeting_receipt_on();                     // cost layout C: Week screen button
void meeting_receipt_show();
void meeting_tick();
void meeting_reset();                          // before the screens are rebuilt

// Procrastination radar (ui_radar.cpp)
void radar_update();                 // after new data: counts moved and missed due dates
int radar_moves(const Task &t);
bool radar_stuck(const Task &t);     // moved at least the "stuck after" setting
// Layout A: glow and a count on a task card; returns the "Moved 6 times since..." line or "".
String radar_mark(lv_obj_t *card, const Task &t);
void radar_nudge_show(const Task &t);  // layout B popup, also a tap in C
bool radar_view_on();                // layout C: Tasks screen button
void radar_view_show();
void radar_tick();

// Task roulette (ui_roulette.cpp)
void roulette_show();

// Phone presence (ui_presence.cpp, Bluetooth in presence.cpp)
bool presence_private();        // layout B, phone away: meeting titles hidden
bool presence_away_screen();    // layout C, phone away: show the night clock
String event_title(const Event &ev);  // "Busy" in privacy mode
String presence_status();       // "1 paired · here now"
void presence_pair_show();
void presence_tick();

bool presence_away();           // feature on and the phone is away

// Smart desk (ui_desk.cpp, network in desk.cpp)
bool desk_tab();                // layouts B and C: a Desk tab in the header
lv_obj_t *mk_desk_dock(lv_obj_t *parent, int w);  // layout A, Home; nullptr if off
lv_obj_t *desk_create();
void desk_refresh();
void desk_tick();
String desk_status_text();      // "80.2 cm · Sitting", or what is wrong

// Marks every screen out of date and rebuilds the visible one.
void ui_refresh_all();

// On-screen keyboard shared by Wi-Fi and quick add.
lv_obj_t *mk_keyboard(lv_obj_t *parent, lv_obj_t *textarea);
lv_obj_t *mk_textarea(lv_obj_t *parent, const char *placeholder);
// Chip button: rounded, small, selected = accent.
lv_obj_t *mk_chip(lv_obj_t *parent, const char *text, bool selected, int h = 40);
void set_chip(lv_obj_t *chip, bool selected);
// Full-screen layer for an overlay. *owner is set to it, and back to nullptr when it is deleted.
lv_obj_t *mk_overlay(bool dim = false, lv_obj_t **owner = nullptr);
void close_overlay(lv_obj_t **layer);

void toast(const String &msg, const char *action = nullptr, lv_event_cb_t cb = nullptr, void *user = nullptr);

// Building blocks
lv_obj_t *mk_box(lv_obj_t *parent, uint32_t bg, int radius = 12);
lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *mk_button(lv_obj_t *parent, uint32_t bg, int radius = 22);
lv_obj_t *mk_text_button(lv_obj_t *parent, const char *text, bool primary, int w = LV_SIZE_CONTENT);
lv_obj_t *mk_icon_button(lv_obj_t *parent, const char *icon, uint32_t color, const lv_font_t *font = &font_16);
lv_obj_t *mk_dot(lv_obj_t *parent, uint32_t color, int size = 8);
lv_obj_t *mk_screen();
lv_obj_t *mk_nav_header(lv_obj_t *scr, Screen active);
lv_obj_t *mk_back_header(lv_obj_t *scr, const char *title, Screen back);
lv_obj_t *mk_switch(lv_obj_t *parent, bool on, lv_event_cb_t cb, void *user = nullptr);
lv_obj_t *mk_scroll_column(lv_obj_t *parent, int gap);
void set_flex(lv_obj_t *obj, lv_flex_flow_t flow, int gap, lv_flex_align_t main = LV_FLEX_ALIGN_START,
              lv_flex_align_t cross = LV_FLEX_ALIGN_CENTER);
void on_click(lv_obj_t *obj, lv_event_cb_t cb, void *user = nullptr);

// Attaches a heap copy of `value` to obj, freed when obj is deleted.
template <typename T>
T *attach(lv_obj_t *obj, const T &value) {
  T *copy = new T(value);
  lv_obj_add_event_cb(obj, [](lv_event_t *e) { delete static_cast<T *>(lv_event_get_user_data(e)); },
                      LV_EVENT_DELETE, copy);
  return copy;
}

// Clock labels in the headers, updated once a minute by ui.cpp.
void register_clock(lv_obj_t *time_label, lv_obj_t *date_label);
// Space in the nav header for the focus timer, leave-by countdown and offline
// badge; ui.cpp keeps every one up to date.
void register_status(lv_obj_t *focus_pill, lv_obj_t *leave_chip, lv_obj_t *offline);

// Formatting (local time)
bool clock_valid();
String fmt_time(time_t t);                 // "14:00" or "2:00 PM"
String fmt_clock();                        // now
String fmt_date_short(time_t t);           // "Thu 29 Oct"
String fmt_date_long(time_t t);            // "Thursday, 29 October"
long day_number(time_t t);                 // local days since 1970
time_t day_start(time_t t);                // local midnight
String day_heading(time_t t);              // "TOMORROW", "SATURDAY 31 OCT"
String event_when(const Event &ev);        // "Today, 14:00 - 15:00"
String until_text(int64_t start_ms);       // "in 1 h 15 min", "now", ""
enum class Tone { None, Warn, Bad };
String due_text(const String &due_key, Tone &tone);  // "Overdue", "Today", "Sat 31 Oct"
String initial_of(const String &s);
