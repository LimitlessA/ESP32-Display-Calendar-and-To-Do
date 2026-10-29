// Shared pieces of the LVGL user interface. Each screen lives in its own
// ui_<screen>.cpp and is built once; lists are rebuilt only when their data
// changes and only while the screen is visible, so screen changes are instant.
#pragma once

#include <lvgl.h>

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
LV_FONT_DECLARE(font_150);

constexpr int SCREEN_W = 800;
constexpr int SCREEN_H = 480;
constexpr int HEADER_H = 56;
constexpr int PAD = 14;
constexpr int GAP = 12;
constexpr int TOUCH = 44;  // smallest touch target

enum class Screen { Home, Detail, Week, Tasks, Night, Wifi, Accounts, Settings, Count };

// Latest data and settings, owned by ui.cpp.
extern Snapshot g_snap;
extern Settings g_settings;

// Provided by the platform (main.cpp on the device, the simulator on a PC).
time_t ui_now();

// Navigation
void ui_show(Screen s);
Screen ui_current();
void ui_open_event(const Event &ev);   // meeting details
void ui_save_settings(const Settings &s);  // saves and applies (theme, brightness)

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
