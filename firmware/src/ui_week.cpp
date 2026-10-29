// Week view, Monday to Sunday (mockup 3).
#include "sync.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

const char *kDayNames[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
constexpr const char *kThisWeek = "This week";
constexpr const char *kLoading = "Loading the week...";
constexpr const char *kFree = "Free";  // day without meetings

constexpr int DAY_GAP = 6;
constexpr int DAY_W = (SCREEN_W - 2 * PAD - 6 * DAY_GAP) / 7;
constexpr int BAR_Y = HEADER_H + 8;
constexpr int COLS_Y = BAR_Y + TOUCH + 8;
constexpr int COLS_H = SCREEN_H - COLS_Y - PAD;

// ---- State -------------------------------------------------------------------

time_t viewWeek = 0;
lv_obj_t *rangeLabel, *cols, *loading;

// ---- Code --------------------------------------------------------------------

time_t addDays(time_t day, int n) {
  struct tm tm;
  localtime_r(&day, &tm);
  tm.tm_mday += n;
  tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
  tm.tm_isdst = -1;
  return mktime(&tm);
}

time_t mondayOf(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  return addDays(day_start(t), -((tm.tm_wday + 6) % 7));
}

void request(time_t week) {
  viewWeek = week;
  if (g_snap.weekStart != week || g_snap.week.empty()) sync_request_week(week);
  week_refresh();
}

void openEvent(lv_event_t *e) {
  ui_open_event(*static_cast<Event *>(lv_event_get_user_data(e)));
}

lv_obj_t *roundButton(lv_obj_t *parent, const char *icon) {
  lv_obj_t *b = mk_button(parent, theme().card, TOUCH / 2);
  lv_obj_set_size(b, TOUCH, TOUCH);
  lv_obj_t *l = mk_label(b, &font_16, theme().text, icon);
  lv_obj_center(l);
  return b;
}

}  // namespace

lv_obj_t *week_create() {
  const Theme &t = theme();
  lv_obj_t *scr = mk_screen();
  mk_nav_header(scr, Screen::Week);

  lv_obj_t *bar = mk_box(scr, 0, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(bar, 18, BAR_Y);
  lv_obj_set_size(bar, SCREEN_W - 18 - 10, TOUCH);
  set_flex(bar, LV_FLEX_FLOW_ROW, 4);
  rangeLabel = mk_label(bar, &font_20b, t.text, "");
  lv_obj_set_flex_grow(rangeLabel, 1);
  on_click(roundButton(bar, ICON_LEFT), [](lv_event_t *) { request(addDays(viewWeek, -7)); });
  lv_obj_t *thisWeek = mk_button(bar, t.card, TOUCH / 2);
  lv_obj_set_size(thisWeek, LV_SIZE_CONTENT, TOUCH);
  lv_obj_set_style_pad_hor(thisWeek, 16, 0);
  lv_obj_center(mk_label(thisWeek, &font_14b, t.text, kThisWeek));
  on_click(thisWeek, [](lv_event_t *) { request(mondayOf(ui_now())); });
  on_click(roundButton(bar, ICON_RIGHT), [](lv_event_t *) { request(addDays(viewWeek, 7)); });

  cols = mk_box(scr, 0, 0);
  lv_obj_set_style_bg_opa(cols, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(cols, PAD, COLS_Y);
  lv_obj_set_size(cols, SCREEN_W - 2 * PAD, COLS_H);
  set_flex(cols, LV_FLEX_FLOW_ROW, DAY_GAP, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  loading = mk_label(scr, &font_16, t.muted, kLoading);
  lv_obj_align(loading, LV_ALIGN_CENTER, 0, 60);
  return scr;
}

void week_on_show() {
  if (!clock_valid()) return;
  request(viewWeek ? viewWeek : mondayOf(ui_now()));
}

void week_refresh() {
  const Theme &t = theme();
  lv_obj_clean(cols);
  if (!viewWeek) {
    lv_label_set_text(rangeLabel, "");
    return;
  }
  const time_t sunday = addDays(viewWeek, 6);
  const String range = fmt_date_short(viewWeek).substring(4) + " - " + fmt_date_short(sunday).substring(4);
  lv_label_set_text(rangeLabel, range.c_str());

  const bool ready = g_snap.weekStart == viewWeek && !g_snap.weekLoading;
  if (ready) lv_obj_add_flag(loading, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(loading, LV_OBJ_FLAG_HIDDEN);

  const long today = day_number(ui_now());
  // Conflicts, layout C: overlapping meetings are outlined and their days marked.
  const bool marks = g_settings.conflicts && g_settings.layout(Layout::Conflicts) == 2;
  const std::vector<Conflict> conflicts =
      marks && ready ? find_conflicts(g_snap.week, viewWeek, addDays(viewWeek, 7)) : std::vector<Conflict>();
  for (int d = 0; d < 7; d++) {
    const time_t day = addDays(viewWeek, d);
    const long dn = day_number(day);
    const bool past = dn < today, isToday = dn == today;

    lv_obj_t *col = mk_box(cols, past ? t.panel : t.card, 12);
    lv_obj_set_size(col, DAY_W, COLS_H);
    lv_obj_set_style_pad_all(col, 6, 0);
    if (isToday) {
      lv_obj_set_style_border_color(col, c(t.accent), 0);
      lv_obj_set_style_border_width(col, 2, 0);
    }
    lv_obj_t *head = mk_box(col, 0, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    lv_obj_set_width(head, lv_pct(100));
    set_flex(head, LV_FLEX_FLOW_COLUMN, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    bool clashDay = false;
    for (const Conflict &cf : conflicts) clashDay |= day_number(cf.from) == dn;
    lv_obj_t *name = mk_label(head, &font_12, isToday ? t.accent : t.muted, kDayNames[d]);
    if (clashDay) {
      lv_label_set_text_fmt(name, "%s  " ICON_WARNING, kDayNames[d]);
      lv_obj_set_style_text_color(name, c(t.badFg), 0);
    }
    lv_obj_set_style_text_letter_space(name, 1, 0);
    struct tm tm;
    localtime_r(&day, &tm);
    lv_obj_t *num = mk_label(head, &font_20b, past ? t.muted : t.text, "");
    lv_label_set_text_fmt(num, "%d", tm.tm_mday);
    if (!ready) continue;

    lv_obj_t *list = mk_scroll_column(col, 6);
    lv_obj_set_pos(list, 0, 46);
    lv_obj_set_size(list, DAY_W - 12, COLS_H - 12 - 46);
    int n = 0;
    for (const Event &ev : g_snap.week) {
      const long s = day_number(ev.start / 1000), e = day_number((ev.end - 1) / 1000);
      if (dn < s || dn > e) continue;  // multi-day events show on every day they cover
      n++;
      lv_obj_t *b = mk_button(list, t.raised, 8);
      lv_obj_set_width(b, DAY_W - 12);
      lv_obj_set_style_pad_all(b, 6, 0);
      set_flex(b, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      lv_obj_t *top = mk_box(b, 0, 0);
      lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
      set_flex(top, LV_FLEX_FLOW_ROW, 5);
      mk_dot(top, ev.color, 7);
      const bool clash = has_conflict(ev, conflicts);
      mk_label(top, &font_12, clash ? t.badFg : t.muted,
               ev.allDay || s != dn ? kAllDay : fmt_time(ev.start / 1000).c_str());
      if (clash) {
        mk_label(top, &font_12, t.badFg, ICON_WARNING);
        lv_obj_set_style_border_color(b, c(t.badFg), 0);
        lv_obj_set_style_border_width(b, 2, 0);
      }
      lv_obj_t *title = mk_label(b, &font_12, t.text, ev.title.c_str());
      lv_obj_set_width(title, DAY_W - 24);
      lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
      on_click(b, openEvent, attach(b, ev));
    }
    if (!n) {
      lv_obj_t *none = mk_label(list, &font_12, t.muted, kFree);
      lv_obj_set_width(none, DAY_W - 12);
      lv_obj_set_style_text_align(none, LV_TEXT_ALIGN_CENTER, 0);
    }
  }
}
