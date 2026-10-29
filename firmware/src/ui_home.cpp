// Home: Today | Coming up | To-do (mockup 1).
#include "sync.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kColToday = "Today";
constexpr const char *kColComingUp = "Coming up";
constexpr const char *kColTodo = "To-do";
constexpr const char *kNowUntil = "Now, until %s";
constexpr const char *kUntil = "Until %s";
constexpr const char *kNoMoreToday = "No more meetings today";
constexpr const char *kWaitingClock = "Waiting for the clock...";
constexpr const char *kNothingUpcoming = "Nothing in the next 7 days";
constexpr const char *kNoAccounts = "Add an account in Settings";
constexpr const char *kAllDone = "All done!";
constexpr const char *kDismissed = "Hidden from the display. Your calendar is unchanged.";
constexpr const char *kDone = "Done: %s";  // toast after ticking a task
constexpr const char *kUndo = "Undo";
constexpr uint32_t kCompleteDelayMs = 450;  // show the tick before the task leaves the list

constexpr const char *kAddTask = "Add a task";
constexpr int DATES_W = 170;  // countdown column, layout A
constexpr int BODY_W = SCREEN_W - 2 * PAD;
constexpr int BODY_H = SCREEN_H - HEADER_H - GAP - PAD;
constexpr int TITLE_H = 30;

// ---- State -------------------------------------------------------------------

lv_obj_t *body;
lv_obj_t *todayList, *todayCount;
lv_obj_t *upcomingList;
lv_obj_t *taskList, *taskCount;
int colW, colH;  // set by each refresh: the strips above and the dates column take space
Task undoTask;  // the task the Undo button brings back

// ---- Code --------------------------------------------------------------------

lv_obj_t *column(lv_obj_t *body, const char *title, lv_obj_t **count) {
  const Theme &t = theme();
  lv_obj_t *col = mk_box(body, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_size(col, colW, colH);
  lv_obj_t *head = mk_box(col, 0, 0);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_size(head, colW, TITLE_H);
  lv_obj_set_style_pad_hor(head, 2, 0);
  set_flex(head, LV_FLEX_FLOW_ROW, 0, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START);
  lv_obj_t *left = mk_box(head, 0, 0);
  lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_grow(left, 1);  // keeps the title on the left when there is no button
  set_flex(left, LV_FLEX_FLOW_ROW, 8);
  mk_label(left, &font_14b, t.text, title);
  if (count) *count = mk_label(left, &font_14, t.muted, "");
  return col;
}

lv_obj_t *list(lv_obj_t *col, int gap) {
  lv_obj_t *l = mk_scroll_column(col, gap);
  lv_obj_set_size(l, colW, colH - TITLE_H);
  lv_obj_set_y(l, TITLE_H);
  return l;
}

void openEvent(lv_event_t *e) {
  ui_open_event(*static_cast<Event *>(lv_event_get_user_data(e)));
}

void dismissEvent(lv_event_t *e) {
  const Event ev = *static_cast<Event *>(lv_event_get_user_data(e));
  // Deleting the row from inside its own click handler is not allowed.
  lv_async_call([](void *p) {
    Event *ev = static_cast<Event *>(p);
    sync_dismiss_event(*ev);
    toast(kDismissed);
    delete ev;
  }, new Event(ev));
}

void undoComplete(lv_event_t *e) {
  sync_uncomplete_task(*static_cast<Task *>(lv_event_get_user_data(e)));
}

}  // namespace

// Shared with the Tasks screen: tick the box, then complete after a beat so
// the check is visible, and offer Undo.
void complete_with_undo(lv_obj_t *box, const Task &task) {
  const Theme &t = theme();
  lv_obj_set_style_bg_color(box, c(t.good), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(box, c(t.good), 0);
  lv_obj_t *mark = mk_label(box, &font_14, t.dark ? t.bg : 0xFFFFFF, ICON_CHECK);
  lv_obj_center(mark);
  lv_timer_t *timer = lv_timer_create([](lv_timer_t *tm) {
    Task *task = static_cast<Task *>(tm->user_data);
    sync_complete_task(*task);
    undoTask = *task;  // the toast outlives this timer
    toast(strf(kDone, task->title.c_str()), kUndo, undoComplete, &undoTask);
    delete task;
    lv_timer_del(tm);
  }, kCompleteDelayMs, new Task(task));
  lv_timer_set_repeat_count(timer, 1);
}

// Round check box inside a 48 px touch target.
lv_obj_t *mk_check(lv_obj_t *parent, const Task &task, int size) {
  const Theme &t = theme();
  lv_obj_t *hit = mk_button(parent, t.card, size / 2);
  lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
  lv_obj_set_size(hit, size, size);
  lv_obj_t *box = mk_box(hit, t.card, 13);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_border_color(box, c(t.muted), 0);
  lv_obj_set_size(box, 26, 26);
  lv_obj_center(box);
  Task *copy = attach(hit, task);
  lv_obj_add_event_cb(hit, [](lv_event_t *e) {
    lv_obj_t *hit = lv_event_get_target(e);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_CLICKABLE);  // no double completes
    complete_with_undo(lv_obj_get_child(hit, 0), *static_cast<Task *>(lv_event_get_user_data(e)));
  }, LV_EVENT_CLICKED, copy);
  return hit;
}

lv_obj_t *home_create() {
  lv_obj_t *scr = mk_screen();
  mk_nav_header(scr, Screen::Home);
  body = mk_box(scr, 0, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(body, PAD, HEADER_H + GAP);
  lv_obj_set_size(body, BODY_W, BODY_H);
  set_flex(body, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  return scr;
}

// Strips above the columns (dates ribbon, conflict banner) and the dates
// column come and go with the data, so the body is laid out on every refresh.
void layoutBody() {
  const Theme &t = theme();
  lv_obj_clean(body);
  colH = BODY_H;
  if (lv_obj_t *r = mk_dates_ribbon(body, BODY_W)) colH -= lv_obj_get_style_height(r, 0) + 8;
  if (lv_obj_t *b = mk_conflict_banner(body, BODY_W)) colH -= lv_obj_get_style_height(b, 0) + 8;
  const bool datesCol = g_settings.layout(Layout::Dates) == 0 && (g_settings.countdowns || g_settings.dates);
  colW = (BODY_W - 2 * GAP - (datesCol ? DATES_W + GAP : 0)) / 3;

  lv_obj_t *row = mk_box(body, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_size(row, BODY_W, colH);
  set_flex(row, LV_FLEX_FLOW_ROW, GAP, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  todayList = list(column(row, kColToday, &todayCount), 8);
  lv_obj_t *mid = column(row, kColComingUp, nullptr);
  upcomingList = list(mid, 0);
  lv_obj_set_style_bg_color(upcomingList, c(t.panel), 0);
  lv_obj_set_style_bg_opa(upcomingList, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(upcomingList, 12, 0);
  lv_obj_set_style_pad_left(upcomingList, 12, 0);
  lv_obj_set_style_pad_ver(upcomingList, 6, 0);
  lv_obj_set_height(upcomingList, LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(upcomingList, colH - TITLE_H, 0);
  lv_obj_t *todo = column(row, kColTodo, &taskCount);
  taskList = list(todo, 8);
  if (g_settings.quickAdd) {
    lv_obj_t *head = lv_obj_get_child(todo, 0);
    lv_obj_t *add = mk_button(head, t.raised, 14);
    lv_obj_set_size(add, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_min_height(add, 28, 0);
    lv_obj_set_style_pad_hor(add, 10, 0);
    lv_obj_set_ext_click_area(add, 8);
    set_flex(add, LV_FLEX_FLOW_ROW, 6);
    mk_label(add, &font_12, t.text, ICON_PLUS);
    mk_label(add, &font_12, t.text, kAddTask);
    on_click(add, [](lv_event_t *) { quick_add_show(); });
  }
  if (datesCol) {
    lv_obj_t *dates = mk_scroll_column(row, 8);
    lv_obj_set_size(dates, DATES_W, colH);
    mk_dates_column(dates, DATES_W);
  }
}

void home_refresh() {
  const Theme &t = theme();
  const time_t now = ui_now();
  const long today = day_number(now);
  layoutBody();
  const std::vector<Conflict> conflicts =
      g_settings.conflicts ? find_conflicts(g_snap.events, now, now + 7 * 86400) : std::vector<Conflict>();
  mk_leave_card(todayList, colW);

  int todayN = 0;
  long lastDay = -1;
  for (const Event &ev : g_snap.events) {
    const time_t start = ev.start / 1000, end = ev.end / 1000;
    if (end <= now) continue;
    const bool isToday = day_number(start) <= today;
    if (isToday) {
      todayN++;
      const bool live = !ev.allDay && start <= now;
      lv_obj_t *card = mk_button(todayList, t.card, 12);
      lv_obj_set_width(card, colW);
      lv_obj_set_style_pad_left(card, 12, 0);
      lv_obj_set_style_pad_ver(card, 10, 0);
      lv_obj_set_style_pad_right(card, TOUCH + 4, 0);
      const bool clash = has_conflict(ev, conflicts);
      if (live || clash) {
        lv_obj_set_style_border_color(card, c(clash ? t.badFg : t.accent), 0);
        lv_obj_set_style_border_width(card, 2, 0);
      }
      set_flex(card, LV_FLEX_FLOW_COLUMN, 3, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      mk_label(card, &font_20b, live ? t.accent : t.text, ev.allDay ? kAllDay : fmt_time(start).c_str());
      lv_obj_t *title = mk_label(card, &font_16s, t.text, ev.title.c_str());
      lv_obj_set_width(title, colW - 12 - TOUCH - 4);
      lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
      lv_obj_t *meta = mk_box(card, 0, 0);
      lv_obj_set_style_bg_opa(meta, LV_OPA_TRANSP, 0);
      set_flex(meta, LV_FLEX_FLOW_ROW, 6);
      mk_dot(meta, ev.color);
      String m = ev.allDay ? String(kAllDay) : strf(live ? kNowUntil : kUntil, fmt_time(end).c_str());
      lv_obj_t *ml = mk_label(meta, &font_12, t.muted, (m + " · " + ev.calendar).c_str());
      lv_label_set_long_mode(ml, LV_LABEL_LONG_DOT);
      lv_obj_set_width(ml, colW - 12 - TOUCH - 4 - 14);
      Event *copy = attach(card, ev);
      on_click(card, openEvent, copy);

      lv_obj_t *x = mk_icon_button(card, ICON_CLOSE, t.muted, &font_14);
      lv_obj_add_flag(x, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_align(x, LV_ALIGN_TOP_RIGHT, TOUCH + 2, -6);
      on_click(x, dismissEvent, copy);
      continue;
    }

    const long day = day_number(start);
    if (day != lastDay) {
      lastDay = day;
      lv_obj_t *h = mk_label(upcomingList, &font_12, t.accent, day_heading(start).c_str());
      lv_obj_set_style_text_letter_space(h, 1, 0);
      lv_obj_set_style_pad_top(h, 8, 0);
    }
    lv_obj_t *row = mk_box(upcomingList, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_size(row, colW - 14, TOUCH);
    set_flex(row, LV_FLEX_FLOW_ROW, 0);
    lv_obj_t *open = mk_button(row, t.panel, 8);
    lv_obj_set_height(open, TOUCH);
    lv_obj_set_flex_grow(open, 1);
    set_flex(open, LV_FLEX_FLOW_ROW, 8);
    mk_dot(open, ev.color);
    const bool clash = has_conflict(ev, conflicts);
    lv_obj_t *tl = mk_label(open, &font_14b, clash ? t.badFg : t.muted, ev.allDay ? kAllDay : fmt_time(start).c_str());
    lv_obj_set_width(tl, g_settings.use24h ? 44 : 64);
    if (clash) mk_label(open, &font_12, t.badFg, ICON_WARNING);
    lv_obj_t *title = mk_label(open, &font_14, t.text, ev.title.c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(title, 1);
    Event *copy = attach(row, ev);
    on_click(open, openEvent, copy);
    lv_obj_t *x = mk_icon_button(row, ICON_CLOSE, t.muted, &font_14);
    on_click(x, dismissEvent, copy);
  }
  if (!todayN) {
    lv_obj_t *empty = mk_box(todayList, t.card, 12);
    lv_obj_set_width(empty, colW);
    lv_obj_set_style_pad_all(empty, 14, 0);
    mk_label(empty, &font_14, t.muted, clock_valid() ? kNoMoreToday : kWaitingClock);
  }
  if (lastDay < 0) {
    lv_obj_t *l = mk_label(upcomingList, &font_14, t.muted, kNothingUpcoming);
    lv_obj_set_style_pad_ver(l, 8, 0);
  }
  lv_label_set_text_fmt(todayCount, "%d", todayN);

  for (const Task &task : g_snap.tasks) {
    if (task.parent.length()) continue;  // subtasks only on the Tasks screen
    lv_obj_t *card = mk_box(taskList, t.card, 12);
    lv_obj_set_width(card, colW);
    lv_obj_set_style_min_height(card, 54, 0);
    lv_obj_set_style_pad_right(card, 10, 0);
    lv_obj_set_style_pad_ver(card, 3, 0);
    set_flex(card, LV_FLEX_FLOW_ROW, 2);
    focus_hold(card, task);
    mk_check(card, task, 48);
    lv_obj_t *text = mk_box(card, 0, 0);
    lv_obj_set_style_bg_opa(text, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(text, 1);
    set_flex(text, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *title = mk_label(text, &font_14b, t.text, task.title.c_str());
    lv_obj_set_width(title, colW - 48 - 14);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    Tone tone;
    const String due = due_text(task.dueKey, tone);
    if (due.length()) {
      mk_label(text, &font_12, tone == Tone::Bad ? t.badFg : tone == Tone::Warn ? t.warnFg : t.muted, due.c_str());
    }
  }
  int open = 0;
  for (const Task &task : g_snap.tasks) open += task.parent.length() == 0;
  if (!open) {
    lv_obj_t *empty = mk_box(taskList, t.card, 12);
    lv_obj_set_width(empty, colW);
    lv_obj_set_style_pad_all(empty, 14, 0);
    mk_label(empty, &font_14, t.muted, g_snap.accounts.empty() ? kNoAccounts : kAllDone);
  }
  lv_label_set_text_fmt(taskCount, "%d", open);
}
