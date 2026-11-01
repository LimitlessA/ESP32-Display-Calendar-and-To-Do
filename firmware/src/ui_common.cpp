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
constexpr const char *kTabDates = "Dates";  // only with the "Dates tab" layout
constexpr const char *kTabDesk = "Desk";    // only with the desk screen layouts
constexpr const char *kSavedCopyHint = "Showing the copy saved on the SD card until the display syncs.";
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
constexpr const char *kDueSep = " \xC2\xB7 ";  // between "Overdue" and the date
constexpr const char *kNoDate = "No date";
constexpr time_t kClockValidAfter = 1700000000;  // NTP has set the clock once it is past this
constexpr int kPillMaxW = 250;  // focus timer pill in the header

// On-screen keyboard layouts
#define BKSP LV_SYMBOL_BACKSPACE
#define OK LV_SYMBOL_OK
const char *kLower[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", BKSP, "\n",
                        "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
                        "ABC", "z", "x", "c", "v", "b", "n", "m", ".", "-", "\n",
                        "1#", " ", OK, ""};
const char *kUpper[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", BKSP, "\n",
                        "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
                        "abc", "Z", "X", "C", "V", "B", "N", "M", ".", "-", "\n",
                        "1#", " ", OK, ""};
const char *kSpecial[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", BKSP, "\n",
                          "!", "@", "#", "$", "%", "&", "*", "(", ")", "\n",
                          "abc", "_", "+", "=", "?", "/", ":", ";", ",", "\"", "\n",
                          "ABC", " ", OK, ""};
#undef BKSP
#undef OK

constexpr lv_btnmatrix_ctrl_t F = LV_KEYBOARD_CTRL_BTN_FLAGS;
const lv_btnmatrix_ctrl_t kCtrl[] = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6 | F,
                                     4, 4, 4, 4, 4, 4, 4, 4, 4,
                                     6 | F, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                                     3 | F, 10, 4 | F};

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

lv_obj_t *mk_textarea(lv_obj_t *parent, const char *placeholder) {
  const Theme &t = theme();
  lv_obj_t *ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_obj_set_height(ta, 48);
  lv_obj_set_style_text_font(ta, &font_16, 0);
  lv_obj_set_style_bg_color(ta, c(t.bg), 0);
  lv_obj_set_style_text_color(ta, c(t.text), 0);
  lv_obj_set_style_border_color(ta, c(t.accent), 0);
  lv_obj_set_style_border_width(ta, 2, 0);
  lv_obj_set_style_radius(ta, 12, 0);
  lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_ver(ta, 12, 0);
  return ta;
}

lv_obj_t *mk_keyboard(lv_obj_t *parent, lv_obj_t *textarea) {
  const Theme &t = theme();
  lv_obj_t *kb = lv_keyboard_create(parent);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, kLower, kCtrl);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, kUpper, kCtrl);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, kSpecial, kCtrl);
  lv_keyboard_set_textarea(kb, textarea);
  lv_obj_set_align(kb, LV_ALIGN_TOP_LEFT);  // the keyboard defaults to bottom-centre
  lv_obj_set_style_bg_color(kb, c(t.panel), 0);
  lv_obj_set_style_radius(kb, 16, 0);
  lv_obj_set_style_pad_all(kb, 8, 0);
  lv_obj_set_style_pad_gap(kb, 5, 0);
  lv_obj_set_style_border_width(kb, 0, 0);
  lv_obj_set_style_bg_color(kb, c(t.raised), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, c(t.line), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(kb, c(t.raised), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(kb, c(t.text), LV_PART_ITEMS);
  lv_obj_set_style_text_font(kb, &font_16, LV_PART_ITEMS);
  lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
  return kb;
}

lv_obj_t *mk_chip(lv_obj_t *parent, const char *text, bool selected, int h) {
  lv_obj_t *b = mk_button(parent, theme().raised, h / 2);
  lv_obj_set_size(b, LV_SIZE_CONTENT, h);
  lv_obj_set_style_min_height(b, h, 0);
  lv_obj_set_style_pad_hor(b, 14, 0);
  lv_obj_t *l = mk_label(b, &font_14b, theme().text, text);
  lv_obj_center(l);
  set_chip(b, selected);
  return b;
}

void set_chip(lv_obj_t *chip, bool selected) {
  const Theme &t = theme();
  lv_obj_set_style_bg_color(chip, c(selected ? t.accent : t.raised), 0);
  lv_obj_t *l = lv_obj_get_child(chip, 0);
  if (l) lv_obj_set_style_text_color(l, c(selected ? t.onAccent : t.text), 0);
}

// ---- Controls: steppers, segmented choices, sliders with end labels ------------

namespace {

struct StepState {
  lv_obj_t *label, *minus, *plus;
  int value, min, max, step;
  StepFmt fmt;
  StepCb cb;
  void *user;
};

void stepShow(StepState *st) {
  lv_label_set_text(st->label, st->fmt(st->value).c_str());
  lv_obj_set_style_opa(st->minus, st->value <= st->min ? LV_OPA_40 : LV_OPA_COVER, 0);
  lv_obj_set_style_opa(st->plus, st->value >= st->max ? LV_OPA_40 : LV_OPA_COVER, 0);
}

void stepBy(lv_event_t *e, int dir) {
  StepState *st = static_cast<StepState *>(lv_event_get_user_data(e));
  int v = st->value + dir * st->step;
  if (v < st->min) v = st->min;
  if (v > st->max) v = st->max;
  if (v == st->value) return;
  st->value = v;
  stepShow(st);
  if (st->cb) st->cb(v, st->user);
}

struct SegItem {
  SegCb cb;
  void *user;
  int index;
};

struct SliderState {
  lv_obj_t *value;
  const char *unit;
};

}  // namespace

lv_obj_t *mk_stepper(lv_obj_t *parent, int value, int min, int max, int step, StepFmt fmt, StepCb cb, void *user,
                     int valueW) {
  const Theme &t = theme();
  lv_obj_t *row = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  set_flex(row, LV_FLEX_FLOW_ROW, 6, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *minus = mk_button(row, t.raised, TOUCH / 2);
  lv_obj_set_size(minus, TOUCH, TOUCH);
  lv_obj_center(mk_label(minus, &font_20b, t.text, "-"));
  lv_obj_t *label = mk_label(row, &font_16, t.text, "");
  lv_obj_set_width(label, valueW);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t *plus = mk_button(row, t.raised, TOUCH / 2);
  lv_obj_set_size(plus, TOUCH, TOUCH);
  lv_obj_center(mk_label(plus, &font_20b, t.text, "+"));
  StepState *st = attach(row, StepState{label, minus, plus, value, min, max, step, fmt, cb, user});
  stepShow(st);
  // A tap steps once; holding repeats (LVGL sends no click after a long press).
  auto down = [](lv_event_t *e) { stepBy(e, -1); };
  auto up = [](lv_event_t *e) { stepBy(e, 1); };
  lv_obj_add_event_cb(minus, down, LV_EVENT_CLICKED, st);
  lv_obj_add_event_cb(minus, down, LV_EVENT_LONG_PRESSED_REPEAT, st);
  lv_obj_add_event_cb(plus, up, LV_EVENT_CLICKED, st);
  lv_obj_add_event_cb(plus, up, LV_EVENT_LONG_PRESSED_REPEAT, st);
  return row;
}

lv_obj_t *mk_segmented(lv_obj_t *parent, const char *const *labels, int count, int selected, int itemW, SegCb cb,
                       void *user) {
  const Theme &t = theme();
  lv_obj_t *seg = mk_box(parent, t.bg, 22);
  lv_obj_set_style_pad_all(seg, 2, 0);
  set_flex(seg, LV_FLEX_FLOW_ROW, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  for (int i = 0; i < count; i++) {
    lv_obj_t *chip = mk_chip(seg, labels[i], i == selected, 40);
    lv_obj_set_width(chip, itemW);
    SegItem *item = attach(chip, SegItem{cb, user, i});
    on_click(chip, [](lv_event_t *e) {
      SegItem *it = static_cast<SegItem *>(lv_event_get_user_data(e));
      lv_obj_t *me = lv_event_get_target(e);
      lv_obj_t *box = lv_obj_get_parent(me);
      for (uint32_t k = 0; k < lv_obj_get_child_cnt(box); k++) {
        lv_obj_t *ch = lv_obj_get_child(box, k);
        set_chip(ch, ch == me);
      }
      if (it->cb) it->cb(it->index, it->user);
    }, item);
  }
  return seg;
}

lv_obj_t *mk_slider(lv_obj_t *parent, const char *title, int value, int min, int max, const char *unit) {
  const Theme &t = theme();
  lv_obj_t *wrap = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
  lv_obj_set_size(wrap, lv_pct(100), 46);
  set_flex(wrap, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  // Title on the left, current value on the right.
  lv_obj_t *head = mk_box(wrap, 0, 0);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_size(head, lv_pct(100), 18);
  set_flex(head, LV_FLEX_FLOW_ROW, 8, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
  mk_label(head, &font_14b, t.text, title);
  lv_obj_t *val = mk_label(head, &font_14b, t.accent, strf("%d%s", value, unit).c_str());
  // The track with a small bar at each end, no min/max text.
  lv_obj_t *scale = mk_box(wrap, 0, 0);
  lv_obj_set_style_bg_opa(scale, LV_OPA_TRANSP, 0);
  lv_obj_set_size(scale, lv_pct(100), 24);
  set_flex(scale, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(scale, 8, 0);
  auto endMark = [&]() {
    lv_obj_t *m = mk_box(scale, t.muted, 1);
    lv_obj_set_size(m, 3, 16);
  };
  endMark();
  lv_obj_t *s = lv_slider_create(scale);
  lv_obj_set_flex_grow(s, 1);
  lv_obj_set_height(s, 8);
  lv_slider_set_range(s, min, max);
  lv_slider_set_value(s, value, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s, c(t.raised), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s, c(t.accent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s, c(t.accent), LV_PART_KNOB);
  lv_obj_set_style_pad_all(s, 6, LV_PART_KNOB);
  lv_obj_set_ext_click_area(s, 16);
  endMark();
  SliderState *st = attach(s, SliderState{val, unit});
  lv_obj_add_event_cb(s, [](lv_event_t *e) {
    SliderState *ss = static_cast<SliderState *>(lv_event_get_user_data(e));
    lv_label_set_text(ss->value, strf("%d%s", (int)lv_slider_get_value(lv_event_get_target(e)), ss->unit).c_str());
  }, LV_EVENT_VALUE_CHANGED, st);
  return s;
}

lv_obj_t *mk_overlay(bool dim, lv_obj_t **owner) {
  const Theme &t = theme();
  lv_obj_t *layer = mk_box(lv_layer_top(), t.dark ? 0x07090B : t.bg, 0);
  lv_obj_set_size(layer, SCREEN_W, SCREEN_H);
  if (dim) {
    lv_obj_set_style_bg_color(layer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(layer, LV_OPA_60, 0);
  }
  lv_obj_add_flag(layer, LV_OBJ_FLAG_CLICKABLE);  // swallow taps behind it
  if (owner) {
    *owner = layer;
    // Forget the layer however it goes, unless the owner has moved on to a newer one.
    lv_obj_add_event_cb(layer, [](lv_event_t *e) {
      lv_obj_t **ref = static_cast<lv_obj_t **>(lv_event_get_user_data(e));
      if (*ref == lv_event_get_target(e)) *ref = nullptr;
    }, LV_EVENT_DELETE, owner);
  }
  return layer;
}

void close_overlay(lv_obj_t **layer) {
  if (*layer) lv_obj_del_async(*layer);
  *layer = nullptr;
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
    {Screen::Dates, ICON_STAR, kTabDates},
    {Screen::Desk, ICON_UPDOWN, kTabDesk},
    {Screen::Settings, ICON_SLIDERS, nullptr},
};

bool datesTab() {
  return g_settings.layout(Layout::Dates) == 2 && (g_settings.countdowns || g_settings.dates);
}

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
  // "Saved copy" badge while the data shown comes from the SD card (before
  // the first sync, or when every account fails). An SD card, not a cloud,
  // so it is not taken for the weather. Tap it for the reason.
  lv_obj_t *offline = mk_label(h, &font_16, t.warnFg, ICON_SD);
  lv_obj_set_ext_click_area(offline, 14);
  on_click(offline, [](lv_event_t *) { toast(g_snap.error.length() ? g_snap.error : String(kSavedCopyHint)); });
  lv_obj_add_flag(offline, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *spacer = mk_box(h, 0, 0);
  lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_grow(spacer, 1);

  // Focus timer pill: icon, time, task, with a thin progress line underneath.
  lv_obj_t *pill = mk_button(h, t.warnBg, 20);
  lv_obj_set_size(pill, LV_SIZE_CONTENT, 40);
  lv_obj_set_style_min_height(pill, 40, 0);
  lv_obj_set_style_max_width(pill, kPillMaxW, 0);
  lv_obj_set_style_pad_hor(pill, 12, 0);
  set_flex(pill, LV_FLEX_FLOW_ROW, 8);
  mk_label(pill, &font_14, t.accent, ICON_TIMER);
  mk_label(pill, &font_14b, t.accent, "");
  lv_obj_t *pt = mk_label(pill, &font_14, t.text, "");
  lv_label_set_long_mode(pt, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(pt, kPillMaxW - 110, 0);
  lv_obj_t *bar = mk_box(pill, t.accent, 1);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(bar, 0, 3);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_add_flag(pill, LV_OBJ_FLAG_HIDDEN);
  on_click(pill, [](lv_event_t *) { focus_open(); });

  // Leave-by countdown (layout C)
  lv_obj_t *leave = mk_button(h, t.accent, 20);
  lv_obj_set_size(leave, LV_SIZE_CONTENT, 40);
  lv_obj_set_style_min_height(leave, 40, 0);
  lv_obj_set_style_pad_hor(leave, 14, 0);
  set_flex(leave, LV_FLEX_FLOW_ROW, 8);
  mk_label(leave, &font_14, t.onAccent, ICON_CAR);
  mk_label(leave, &font_14b, t.onAccent, "");
  lv_obj_add_flag(leave, LV_OBJ_FLAG_HIDDEN);
  on_click(leave, [](lv_event_t *) {
    time_t at;
    const Event *ev = leave_next(at);
    if (ev) ui_open_event(*ev);
  });
  register_status(pill, leave, offline);

  lv_obj_t *nav = mk_box(h, 0, 0);
  lv_obj_set_style_bg_opa(nav, LV_OPA_TRANSP, 0);
  set_flex(nav, LV_FLEX_FLOW_ROW, 4);
  for (const Tab &tab : kTabs) {
    if (tab.screen == Screen::Dates && !datesTab()) continue;
    if (tab.screen == Screen::Desk && !desk_tab()) continue;
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
    return String(kOverdue) + kDueSep + fmt_date_short(due);
  }
  if (diff == 0) {
    tone = Tone::Warn;
    return kToday;
  }
  if (diff == 1) return String(kTomorrow) + kDueSep + fmt_date_short(due);
  return fmt_date_short(due);
}

lv_obj_t *mk_due(lv_obj_t *parent, const String &due_key) {
  const Theme &t = theme();
  Tone tone;
  const String text = due_text(due_key, tone);
  lv_obj_t *row = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  set_flex(row, LV_FLEX_FLOW_ROW, 6);
  if (!text.length()) {
    mk_label(row, &font_14, t.muted, kNoDate);
    return row;
  }
  const uint32_t color = tone == Tone::Bad ? t.badFg : tone == Tone::Warn ? t.warnFg
                         : text.startsWith(kTomorrow) ? t.text : t.muted;
  mk_label(row, &font_14, color, tone == Tone::Bad ? ICON_WARNING : tone == Tone::Warn ? ICON_CLOCK : ICON_CALENDAR);
  mk_label(row, &font_14, color, text.c_str());
  return row;
}

String day_key(time_t t) {
  const struct tm tm = local(t);
  return strf("%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

long days_until(const String &key) {
  if (key.length() < 10) return 0;
  struct tm tm = {};
  tm.tm_year = key.substring(0, 4).toInt() - 1900;
  tm.tm_mon = key.substring(5, 7).toInt() - 1;
  tm.tm_mday = key.substring(8, 10).toInt();
  tm.tm_hour = 12;
  tm.tm_isdst = -1;
  return day_number(mktime(&tm)) - day_number(ui_now());
}

String initial_of(const String &s) {
  return s.length() ? upper(s.substring(0, 1)) : String("?");
}
