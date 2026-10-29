#include <stdarg.h>
#include <time.h>

#include "ui_internal.h"

// ---- Texts and settings ------------------------------------------------------

// Shared with other screens through ui_internal.h
const char *const kToday = "Today";
const char *const kTomorrow = "Tomorrow";
const char *const kAllDay = "All day";

namespace {

const char *kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *kMonths[] = {"January", "February", "March", "April", "May", "June",
                         "July", "August", "September", "October", "November", "December"};
constexpr const char *kTabHome = "Home";
constexpr const char *kTabWeek = "Week";
constexpr const char *kTabTasks = "Tasks";
constexpr const char *kAm = " AM";
constexpr const char *kPm = " PM";
constexpr const char *kNoClock = "--:--";  // before the clock is set
constexpr const char *kTodayHeading = "TODAY";
constexpr const char *kTomorrowHeading = "TOMORROW";
constexpr const char *kDaysSpan = ", %d days";  // all-day event over several days
constexpr const char *kAllDaySuffix = ", all day";
constexpr const char *kNow = "now";
constexpr const char *kInMinutes = "in %d min";
constexpr const char *kInHours = "in %d h";
constexpr const char *kInHoursMinutes = "in %d h %d min";
constexpr const char *kInDays = "in %d days";
constexpr const char *kOverdue = "Overdue";
constexpr time_t kClockValidAfter = 1700000000;  // NTP has set the clock once it is past this

struct tm local(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  return tm;
}

String upper(const String &s) {
  String out = s;
  out.toUpperCase();
  return out;
}

String num(int n) {
  return String(n);
}

String two(int n) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%02d", n);
  return buf;
}

// Pressed state: a little lighter on dark themes, a little darker on light ones.
lv_color_t pressed(uint32_t base) {
  return theme().dark ? lv_color_mix(lv_color_white(), c(base), 28) : lv_color_mix(lv_color_black(), c(base), 20);
}

}  // namespace

String strf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  return buf;
}

// ---- Building blocks -------------------------------------------------------

void set_flex(lv_obj_t *obj, lv_flex_flow_t flow, int gap, lv_flex_align_t main, lv_flex_align_t cross) {
  lv_obj_set_flex_flow(obj, flow);
  lv_obj_set_flex_align(obj, main, cross, cross);
  lv_obj_set_style_pad_row(obj, gap, 0);
  lv_obj_set_style_pad_column(obj, gap, 0);
}

void on_click(lv_obj_t *obj, lv_event_cb_t cb, void *user) {
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user);
}

lv_obj_t *mk_box(lv_obj_t *parent, uint32_t bg, int radius) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_style_bg_color(o, c(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  return o;
}

lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, c(color), 0);
  lv_label_set_text(l, text);
  return l;
}

lv_obj_t *mk_button(lv_obj_t *parent, uint32_t bg, int radius) {
  lv_obj_t *b = mk_box(parent, bg, radius);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(b, pressed(bg), LV_STATE_PRESSED);
  lv_obj_set_style_min_height(b, TOUCH, 0);
  return b;
}

lv_obj_t *mk_text_button(lv_obj_t *parent, const char *text, bool primary, int w) {
  const Theme &t = theme();
  lv_obj_t *b = mk_button(parent, primary ? t.accent : t.raised);
  lv_obj_set_size(b, w, TOUCH);
  lv_obj_set_style_pad_hor(b, 16, 0);
  lv_obj_t *l = mk_label(b, &font_14b, primary ? t.onAccent : t.text, text);
  lv_obj_center(l);
  return b;
}

lv_obj_t *mk_icon_button(lv_obj_t *parent, const char *icon, uint32_t color, const lv_font_t *font) {
  lv_obj_t *b = mk_button(parent, theme().card, TOUCH / 2);
  lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(b, c(theme().raised), LV_STATE_PRESSED);
  lv_obj_set_size(b, TOUCH, TOUCH);
  lv_obj_t *l = mk_label(b, font, color, icon);
  lv_obj_center(l);
  return b;
}

lv_obj_t *mk_dot(lv_obj_t *parent, uint32_t color, int size) {
  lv_obj_t *d = mk_box(parent, color, size / 2);
  lv_obj_set_size(d, size, size);
  return d;
}

lv_obj_t *mk_screen() {
  lv_obj_t *s = lv_obj_create(nullptr);
  lv_obj_remove_style_all(s);
  lv_obj_set_style_bg_color(s, c(theme().bg), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(s, c(theme().text), 0);
  lv_obj_set_style_text_font(s, &font_16, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

lv_obj_t *mk_scroll_column(lv_obj_t *parent, int gap) {
  lv_obj_t *col = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(col, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_ACTIVE);
  set_flex(col, LV_FLEX_FLOW_COLUMN, gap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  return col;
}

lv_obj_t *mk_switch(lv_obj_t *parent, bool on, lv_event_cb_t cb, void *user) {
  const Theme &t = theme();
  lv_obj_t *sw = lv_switch_create(parent);
  lv_obj_set_size(sw, 56, 32);
  lv_obj_set_style_bg_color(sw, c(t.line), LV_PART_MAIN);
  lv_obj_set_style_bg_color(sw, c(t.accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(sw, c(t.dark ? t.bg : 0xFFFFFF), LV_PART_KNOB);
  lv_obj_set_style_pad_all(sw, -3, LV_PART_KNOB);
  // Bigger hit area than the drawn switch.
  lv_obj_set_ext_click_area(sw, 8);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, user);
  return sw;
}

// ---- Headers ---------------------------------------------------------------

namespace {

struct Tab {
  Screen screen;
  const char *icon;
  const char *label;
};

const Tab kTabs[] = {
    {Screen::Home, ICON_HOME, kTabHome},
    {Screen::Week, ICON_CALENDAR, kTabWeek},
    {Screen::Tasks, ICON_TASKS, kTabTasks},
    {Screen::Settings, ICON_SLIDERS, nullptr},
};

void go(lv_event_t *e) {
  ui_show(static_cast<Screen>((intptr_t)lv_event_get_user_data(e)));
}

}  // namespace

lv_obj_t *mk_nav_header(lv_obj_t *scr, Screen active) {
  const Theme &t = theme();
  lv_obj_t *h = mk_box(scr, t.header, 0);
  lv_obj_set_size(h, SCREEN_W, HEADER_H);
  lv_obj_set_style_pad_left(h, 18, 0);
  lv_obj_set_style_pad_right(h, 10, 0);
  set_flex(h, LV_FLEX_FLOW_ROW, 12, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *clock = mk_label(h, &font_26b, t.text, "");
  lv_obj_t *date = mk_label(h, &font_16, t.muted, "");
  register_clock(clock, date);

  lv_obj_t *spacer = mk_box(h, 0, 0);
  lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_grow(spacer, 1);

  lv_obj_t *nav = mk_box(h, 0, 0);
  lv_obj_set_style_bg_opa(nav, LV_OPA_TRANSP, 0);
  set_flex(nav, LV_FLEX_FLOW_ROW, 4);
  for (const Tab &tab : kTabs) {
    const bool on = tab.screen == active;
    lv_obj_t *b = mk_button(nav, on ? t.raised : t.header);
    lv_obj_set_height(b, TOUCH);
    lv_obj_set_style_pad_hor(b, tab.label ? 14 : 12, 0);
    set_flex(b, LV_FLEX_FLOW_ROW, 8);
    mk_label(b, &font_16, on ? t.text : t.muted, tab.icon);
    if (tab.label) mk_label(b, &font_14b, on ? t.text : t.muted, tab.label);
    on_click(b, go, (void *)(intptr_t)tab.screen);
  }
  return h;
}

lv_obj_t *mk_back_header(lv_obj_t *scr, const char *title, Screen back) {
  const Theme &t = theme();
  lv_obj_t *h = mk_box(scr, t.header, 0);
  lv_obj_set_size(h, SCREEN_W, HEADER_H);
  lv_obj_set_style_pad_left(h, 8, 0);
  lv_obj_set_style_pad_right(h, 12, 0);
  set_flex(h, LV_FLEX_FLOW_ROW, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *b = mk_button(h, t.header);
  lv_obj_set_height(b, TOUCH);
  lv_obj_set_style_pad_left(b, 10, 0);
  lv_obj_set_style_pad_right(b, 14, 0);
  set_flex(b, LV_FLEX_FLOW_ROW, 10);
  mk_label(b, &font_16, t.text, ICON_LEFT);
  mk_label(b, &font_20b, t.text, title);
  on_click(b, go, (void *)(intptr_t)back);

  lv_obj_t *spacer = mk_box(h, 0, 0);
  lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_grow(spacer, 1);
  return h;
}

// ---- Formatting ------------------------------------------------------------

bool clock_valid() {
  return ui_now() > kClockValidAfter;
}

String fmt_time(time_t t) {
  const struct tm tm = local(t);
  if (g_settings.use24h) return two(tm.tm_hour) + ":" + two(tm.tm_min);
  const int h = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
  return num(h) + ":" + two(tm.tm_min) + (tm.tm_hour < 12 ? kAm : kPm);
}

String fmt_clock() {
  return clock_valid() ? fmt_time(ui_now()) : String(kNoClock);
}

String fmt_date_short(time_t t) {
  const struct tm tm = local(t);
  return String(kDays[tm.tm_wday]).substring(0, 3) + " " + num(tm.tm_mday) + " " + String(kMonths[tm.tm_mon]).substring(0, 3);
}

String fmt_date_long(time_t t) {
  const struct tm tm = local(t);
  return String(kDays[tm.tm_wday]) + ", " + num(tm.tm_mday) + " " + kMonths[tm.tm_mon];
}

time_t day_start(time_t t) {
  struct tm tm = local(t);
  tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
  tm.tm_isdst = -1;
  return mktime(&tm);
}

long day_number(time_t t) {
  const struct tm tm = local(t);
  // Days from civil (Howard Hinnant), independent of the time zone offset.
  int y = tm.tm_year + 1900;
  const int m = tm.tm_mon + 1, d = tm.tm_mday;
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

String day_heading(time_t t) {
  const long diff = day_number(t) - day_number(ui_now());
  if (diff == 0) return kTodayHeading;
  if (diff == 1) return kTomorrowHeading;
  const struct tm tm = local(t);
  return upper(String(kDays[tm.tm_wday]) + " " + num(tm.tm_mday) + " " + String(kMonths[tm.tm_mon]).substring(0, 3));
}

String event_when(const Event &ev) {
  const time_t s = ev.start / 1000, e = ev.end / 1000;
  const long diff = day_number(s) - day_number(ui_now());
  String day = diff == 0 ? String(kToday) : diff == 1 ? String(kTomorrow) : fmt_date_short(s);
  if (ev.allDay) {
    const long days = day_number(e - 1) - day_number(s) + 1;
    return day + (days > 1 ? strf(kDaysSpan, (int)days) : String(kAllDaySuffix));
  }
  return day + ", " + fmt_time(s) + " - " + fmt_time(e);
}

String until_text(int64_t start_ms) {
  const long mins = (long)((start_ms / 1000 - ui_now()) / 60);
  if (mins < 0) return "";
  if (mins == 0) return kNow;
  if (mins < 60) return strf(kInMinutes, (int)mins);
  if (mins < 24 * 60) {
    return mins % 60 ? strf(kInHoursMinutes, (int)(mins / 60), (int)(mins % 60)) : strf(kInHours, (int)(mins / 60));
  }
  return strf(kInDays, (int)((mins + 12 * 60) / (24 * 60)));
}

String due_text(const String &due_key, Tone &tone) {
  tone = Tone::None;
  if (due_key.length() < 10) return "";
  struct tm tm = {};
  tm.tm_year = due_key.substring(0, 4).toInt() - 1900;
  tm.tm_mon = due_key.substring(5, 7).toInt() - 1;
  tm.tm_mday = due_key.substring(8, 10).toInt();
  tm.tm_hour = 12;
  tm.tm_isdst = -1;
  const time_t due = mktime(&tm);
  const long diff = day_number(due) - day_number(ui_now());
  if (diff < 0) {
    tone = Tone::Bad;
    return kOverdue;
  }
  if (diff == 0) {
    tone = Tone::Warn;
    return kToday;
  }
  if (diff == 1) return kTomorrow;
  return fmt_date_short(due);
}

String initial_of(const String &s) {
  return s.length() ? upper(s.substring(0, 1)) : String("?");
}
