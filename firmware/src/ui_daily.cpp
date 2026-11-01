// Daily summary every morning (mockup row 3: A stat tiles, B a timeline,
// C a briefing) and the end-of-day review every evening (row 8: A a
// checklist, B one task at a time, C plan by day).
#include "sync.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kMorning = "Good morning";
constexpr const char *kStartDay = "Start my day";
constexpr const char *kMeetings = "Meetings";
constexpr const char *kFirstAt = "First at";
constexpr const char *kDueToday = "Due today";
constexpr const char *kOverdue = "Overdue";
constexpr const char *kNone = "-";
constexpr const char *kTasksToday = "TASKS FOR TODAY";
constexpr const char *kFreeDay = "No meetings today.";
constexpr const char *kNoTasks = "No tasks due.";
constexpr const char *kBriefMeetings = "You have %d meetings today. The first is %s at %s.";
constexpr const char *kBriefOneMeeting = "You have one meeting today: %s at %s.";
constexpr const char *kBriefTasks = "%d tasks are due today.";
constexpr const char *kBriefOneTask = "One task is due today: %s.";
constexpr const char *kBriefOverdue = "%d overdue from before.";
constexpr const char *kBriefLeave = "Leave by %s for %s.";
constexpr const char *kBriefDate = "%s today.";  // birthday or holiday title
constexpr const char *kBriefConflict = "%s and %s overlap.";
constexpr int kTimelineFrom = 8, kTimelineTo = 20;  // hours shown in layout B

constexpr const char *kReviewTitle = "End of the day";
constexpr const char *kReviewSub = "%d tasks are still open. What happens to them?";
constexpr const char *kReviewNothing = "Everything is done. Nice work!";
constexpr const char *kTomorrowBtn = "Tomorrow";
constexpr const char *kNextWeek = "Next week";
constexpr const char *kDoneBtn = "Done";
constexpr const char *kSkip = "Skip";
constexpr const char *kAllTomorrow = "Move all to tomorrow";
constexpr const char *kFinish = "Finish";
constexpr const char *kClose = "Close";
constexpr const char *kOneOf = "%d OF %d";
constexpr const char *kPlanHint = "Pick a day for each task. It moves in Google Tasks right away.";
constexpr const char *kMoved = "Moved to %s";
constexpr const char *kNoDate = "No date";
constexpr int kPlanDays = 4;  // C: tomorrow and the next three days
constexpr uint32_t kCheckMs = 5000;
constexpr int kLateMinutes = 120;  // still show it this long after its time (e.g. after night mode)

// ---- State -------------------------------------------------------------------

lv_obj_t *layer = nullptr;
std::vector<Task> pending;    // review: tasks due today or overdue
size_t current = 0;        // review B: task on screen
long summaryDay = -1, reviewDay = -1;
uint32_t lastCheck = 0;

// ---- Code --------------------------------------------------------------------

lv_obj_t *transparent(lv_obj_t *parent) {
  lv_obj_t *o = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  return o;
}

void close() { close_overlay(&layer); }

struct Day {
  std::vector<Event> events;   // timed and all-day events today
  std::vector<Task> due, overdue;
  std::vector<DateItem> dates;
};

Day today() {
  Day d;
  const time_t now = ui_now();
  const long dn = day_number(now);
  for (const Event &e : g_snap.events) {
    if (day_number(e.start / 1000) <= dn && e.end / 1000 > day_start(now)) d.events.push_back(e);
  }
  for (const Task &t : g_snap.tasks) {
    if (t.parent.length()) continue;
    Tone tone;
    due_text(t.dueKey, tone);
    if (tone == Tone::Warn) d.due.push_back(t);
    if (tone == Tone::Bad) d.overdue.push_back(t);
  }
  for (const DateItem &di : g_snap.dates) {
    const bool on = di.kind == DateKind::Star ? g_settings.countdowns : g_settings.dates;
    if (on && days_until(di.day) == 0) d.dates.push_back(di);
  }
  return d;
}

const Event *firstTimed(const Day &d) {
  for (const Event &e : d.events) {
    if (!e.allDay) return &e;
  }
  return nullptr;
}

// Greeting and date at the top left, close button at the bottom.
void frame(const char *title, const String &sub) {
  const Theme &t = theme();
  lv_obj_t *h = mk_label(layer, &font_34b, t.text, title);
  lv_obj_set_pos(h, 40, 28);
  lv_obj_t *s = mk_label(layer, &font_16, t.muted, sub.c_str());
  lv_obj_set_pos(s, 42, 74);
  lv_obj_set_width(s, SCREEN_W - 84);
  lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
}

lv_obj_t *footerButton(const char *text, bool primary, lv_event_cb_t cb, int w = 220) {
  lv_obj_t *b = mk_text_button(layer, text, primary, w);
  lv_obj_set_height(b, 56);
  lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, -40, -28);
  on_click(b, cb);
  return b;
}

void startDay(lv_event_t *) { close(); }

void tile(lv_obj_t *parent, const char *label, const String &value, uint32_t color, int w) {
  const Theme &t = theme();
  lv_obj_t *b = mk_box(parent, t.card, 16);
  lv_obj_set_size(b, w, 130);
  lv_obj_set_style_pad_all(b, 18, 0);
  set_flex(b, LV_FLEX_FLOW_COLUMN, 6, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  mk_label(b, &font_14, t.muted, label);
  mk_label(b, &font_34b, color, value.c_str());
}

// A: four numbers and a line for what else is today.
void summaryTiles(const Day &d) {
  const Theme &t = theme();
  frame(kMorning, fmt_date_long(ui_now()));
  lv_obj_t *row = transparent(layer);
  lv_obj_set_pos(row, 40, 120);
  lv_obj_set_size(row, SCREEN_W - 80, 130);
  set_flex(row, LV_FLEX_FLOW_ROW, 14);
  const int w = (SCREEN_W - 80 - 3 * 14) / 4;
  const Event *first = firstTimed(d);
  tile(row, kMeetings, String((int)d.events.size()), t.text, w);
  tile(row, kFirstAt, first ? fmt_time(first->start / 1000) : String(kNone), t.accent, w);
  tile(row, kDueToday, String((int)d.due.size()), d.due.empty() ? t.text : t.warnFg, w);
  tile(row, kOverdue, String((int)d.overdue.size()), d.overdue.empty() ? t.text : t.badFg, w);

  lv_obj_t *list = transparent(layer);
  lv_obj_set_pos(list, 40, 270);
  lv_obj_set_size(list, SCREEN_W - 80 - 240, 180);
  set_flex(list, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  int n = 0;
  for (const Event &e : d.events) {
    if (n++ >= 4) break;
    lv_obj_t *r = transparent(list);
    set_flex(r, LV_FLEX_FLOW_ROW, 10);
    mk_dot(r, e.color);
    lv_obj_t *tl = mk_label(r, &font_14b, t.muted, e.allDay ? kAllDay : fmt_time(e.start / 1000).c_str());
    lv_obj_set_width(tl, 64);
    lv_obj_t *ti = mk_label(r, &font_16, t.text, e.title.c_str());
    lv_label_set_long_mode(ti, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ti, SCREEN_W - 80 - 240 - 100);
  }
  for (const DateItem &di : d.dates) {
    mk_label(list, &font_14b, t.accent, (String(ICON_STAR) + "  " + di.title).c_str());
  }
  if (d.events.empty()) mk_label(list, &font_16, t.muted, kFreeDay);
  footerButton(kStartDay, true, startDay);
}

// B: the day from 8 to 20 as a strip, tasks below.
void summaryTimeline(const Day &d) {
  const Theme &t = theme();
  frame(kMorning, fmt_date_long(ui_now()));
  const int x0 = 40, w = SCREEN_W - 80, y = 130, h = 92;
  const time_t base = day_start(ui_now());
  lv_obj_t *strip = mk_box(layer, t.panel, 12);
  lv_obj_set_pos(strip, x0, y);
  lv_obj_set_size(strip, w, h);
  const float perHour = (float)w / (kTimelineTo - kTimelineFrom);
  for (int hr = kTimelineFrom; hr <= kTimelineTo; hr += 2) {
    lv_obj_t *l = mk_label(layer, &font_12, t.muted, fmt_time(base + hr * 3600).c_str());
    lv_obj_set_pos(l, x0 + (int)((hr - kTimelineFrom) * perHour) - (hr == kTimelineTo ? 40 : hr == kTimelineFrom ? 0 : 18),
                   y + h + 6);
  }
  for (const Event &e : d.events) {
    if (e.allDay) continue;
    float from = (float)(e.start / 1000 - base) / 3600.0f, to = (float)(e.end / 1000 - base) / 3600.0f;
    if (to <= kTimelineFrom || from >= kTimelineTo) continue;
    if (from < kTimelineFrom) from = kTimelineFrom;
    if (to > kTimelineTo) to = kTimelineTo;
    const int bx = (int)((from - kTimelineFrom) * perHour), bw = (int)((to - from) * perHour);
    lv_obj_t *b = mk_box(strip, e.color, 8);
    lv_obj_set_pos(b, bx + 2, 8);
    lv_obj_set_size(b, bw > 6 ? bw - 4 : 4, h - 16);
    lv_obj_set_style_pad_all(b, 6, 0);
    if (bw > 70) {
      lv_obj_t *l = mk_label(b, &font_12, 0x111111, e.title.c_str());
      lv_obj_set_width(l, bw - 16);
      lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
  }
  // Now marker
  const float nowH = (float)(ui_now() - base) / 3600.0f;
  if (nowH > kTimelineFrom && nowH < kTimelineTo) {
    lv_obj_t *m = mk_box(strip, t.accent, 0);
    lv_obj_set_pos(m, (int)((nowH - kTimelineFrom) * perHour), 0);
    lv_obj_set_size(m, 2, h);
  }

  lv_obj_t *head = mk_label(layer, &font_12, t.muted, kTasksToday);
  lv_obj_set_style_text_letter_space(head, 1, 0);
  lv_obj_set_pos(head, 40, 262);
  lv_obj_t *list = transparent(layer);
  lv_obj_set_pos(list, 40, 284);
  lv_obj_set_size(list, SCREEN_W - 80 - 240, 170);
  set_flex(list, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  int n = 0;
  for (const std::vector<Task> *v : {&d.overdue, &d.due}) {
    for (const Task &task : *v) {
      if (n++ >= 5) break;
      lv_obj_t *r = transparent(list);
      set_flex(r, LV_FLEX_FLOW_ROW, 10);
      mk_dot(r, v == &d.overdue ? t.badFg : t.warnFg);
      lv_obj_t *l = mk_label(r, &font_16, t.text, task.title.c_str());
      lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
      lv_obj_set_width(l, SCREEN_W - 80 - 240 - 30 - 200);
      mk_due(r, task.dueKey);
    }
  }
  if (!n) mk_label(list, &font_16, t.muted, kNoTasks);
  footerButton(kStartDay, true, startDay);
}

// C: a few sentences, like a news briefing.
void summaryBriefing(const Day &d) {
  const Theme &t = theme();
  lv_obj_t *clock = mk_label(layer, &font_72, t.text, fmt_clock().c_str());
  lv_obj_set_pos(clock, 40, 30);
  lv_obj_t *date = mk_label(layer, &font_20b, t.accent, fmt_date_long(ui_now()).c_str());
  lv_obj_set_pos(date, 44, 120);

  std::vector<String> lines;
  const Event *first = firstTimed(d);
  if (d.events.size() == 1 && first) {
    lines.push_back(strf(kBriefOneMeeting, first->title.c_str(), fmt_time(first->start / 1000).c_str()));
  } else if (first) {
    lines.push_back(strf(kBriefMeetings, (int)d.events.size(), first->title.c_str(), fmt_time(first->start / 1000).c_str()));
  } else {
    lines.push_back(kFreeDay);
  }
  if (g_settings.conflicts) {
    const time_t now = ui_now();
    const std::vector<Conflict> cf = find_conflicts(d.events, now, day_start(now) + 86400);
    if (!cf.empty()) lines.push_back(strf(kBriefConflict, cf[0].a.title.c_str(), cf[0].b.title.c_str()));
  }
  for (const Event &e : d.events) {
    if (g_settings.leaveBy && e.travelMin) {
      lines.push_back(strf(kBriefLeave, fmt_time(e.start / 1000 - e.travelMin * 60).c_str(), e.title.c_str()));
      break;
    }
  }
  if (d.due.size() == 1) lines.push_back(strf(kBriefOneTask, d.due[0].title.c_str()));
  else if (d.due.size()) lines.push_back(strf(kBriefTasks, (int)d.due.size()));
  else lines.push_back(kNoTasks);
  if (d.overdue.size()) lines.push_back(strf(kBriefOverdue, (int)d.overdue.size()));
  for (const DateItem &di : d.dates) lines.push_back(strf(kBriefDate, di.title.c_str()));

  lv_obj_t *col = transparent(layer);
  lv_obj_set_pos(col, 40, 170);
  lv_obj_set_size(col, SCREEN_W - 80, 220);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  for (const String &l : lines) {
    lv_obj_t *r = transparent(col);
    set_flex(r, LV_FLEX_FLOW_ROW, 14, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *bar = mk_box(r, t.accent, 2);
    lv_obj_set_size(bar, 4, 22);
    lv_obj_t *text = mk_label(r, &font_20b, t.text, l.c_str());
    lv_obj_set_width(text, SCREEN_W - 80 - 20);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
  }
  footerButton(kStartDay, true, startDay);
}

// ---- Review --------------------------------------------------------------------

std::vector<Task> openTasks() {
  std::vector<Task> out;
  const Day d = today();
  out = d.overdue;
  out.insert(out.end(), d.due.begin(), d.due.end());
  return out;
}

String inDays(int n) { return day_key(ui_now() + n * 86400L); }

void moveTask(const Task &t, const String &key) {
  sync_set_task_due(t, key);
}

void finish(lv_event_t *) { close(); }

void buildReview();

// A: every open task with a check box and a Tomorrow button.
void reviewChecklist() {
  const Theme &t = theme();
  frame(kReviewTitle, strf(kReviewSub, (int)pending.size()));
  lv_obj_t *list = mk_scroll_column(layer, 8);
  lv_obj_set_pos(list, 40, 110);
  lv_obj_set_size(list, SCREEN_W - 80, SCREEN_H - 110 - 100);
  for (const Task &task : pending) {
    lv_obj_t *row = mk_box(list, t.card, 12);
    lv_obj_set_size(row, SCREEN_W - 80, 60);
    lv_obj_set_style_pad_hor(row, 6, 0);
    set_flex(row, LV_FLEX_FLOW_ROW, 6);
    mk_check(row, task, 48);
    lv_obj_t *title = mk_label(row, &font_16s, t.text, task.title.c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(title, 1);
    mk_due(row, task.dueKey);
    lv_obj_t *tm = mk_chip(row, kTomorrowBtn, false);
    Task *copy = attach(tm, task);
    on_click(tm, [](lv_event_t *e) {
      lv_obj_t *chip = lv_event_get_target(e);
      moveTask(*static_cast<Task *>(lv_event_get_user_data(e)), inDays(1));
      set_chip(chip, true);
      lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    }, copy);
  }
  lv_obj_t *all = mk_text_button(layer, kAllTomorrow, false, 260);
  lv_obj_set_height(all, 56);
  lv_obj_align(all, LV_ALIGN_BOTTOM_LEFT, 40, -28);
  on_click(all, [](lv_event_t *) {
    for (const Task &task : openTasks()) moveTask(task, inDays(1));
    toast(strf(kMoved, kTomorrowBtn));
    close();
  });
  footerButton(kFinish, true, finish);
}

void reviewNext() {
  current++;
  lv_async_call([](void *) { buildReview(); }, nullptr);
}

// B: one big card at a time.
void reviewOneByOne() {
  const Theme &t = theme();
  if (current >= pending.size()) {
    close();
    return;
  }
  const Task &task = pending[current];
  lv_obj_t *step = mk_label(layer, &font_14b, t.accent, strf(kOneOf, (int)current + 1, (int)pending.size()).c_str());
  lv_obj_set_style_text_letter_space(step, 1, 0);
  lv_obj_align(step, LV_ALIGN_TOP_MID, 0, 36);
  lv_obj_t *card = mk_box(layer, t.card, 24);
  lv_obj_set_size(card, 620, 200);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 72);
  lv_obj_set_style_pad_all(card, 28, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *title = mk_label(card, &font_26b, t.text, task.title.c_str());
  lv_obj_set_width(title, 560);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  mk_due(card, task.dueKey);
  mk_label(card, &font_14, t.muted, task.list.c_str());

  lv_obj_t *row = transparent(layer);
  lv_obj_set_size(row, 620, 64);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 300);
  set_flex(row, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_CENTER);
  auto big = [&](const char *text, bool primary, lv_event_cb_t cb) {
    lv_obj_t *b = mk_text_button(row, text, primary, 145);
    lv_obj_set_height(b, 64);
    on_click(b, cb);
  };
  big(kDoneBtn, true, [](lv_event_t *) {
    sync_complete_task(pending[current]);
    play(Sound::Done);
    reviewNext();
  });
  big(kTomorrowBtn, false, [](lv_event_t *) {
    moveTask(pending[current], inDays(1));
    reviewNext();
  });
  big(kNextWeek, false, [](lv_event_t *) {
    moveTask(pending[current], inDays(7));
    reviewNext();
  });
  big(kSkip, false, [](lv_event_t *) { reviewNext(); });
  lv_obj_t *x = mk_text_button(layer, kClose, false, 160);
  lv_obj_align(x, LV_ALIGN_BOTTOM_MID, 0, -28);
  on_click(x, finish);
}

// C: a row per task with chips for the next days.
void reviewPlan() {
  const Theme &t = theme();
  frame(kReviewTitle, kPlanHint);
  lv_obj_t *list = mk_scroll_column(layer, 8);
  lv_obj_set_pos(list, 40, 110);
  lv_obj_set_size(list, SCREEN_W - 80, SCREEN_H - 110 - 100);
  const time_t now = ui_now();
  for (const Task &task : pending) {
    lv_obj_t *row = mk_box(list, t.card, 12);
    lv_obj_set_size(row, SCREEN_W - 80, 60);
    lv_obj_set_style_pad_hor(row, 14, 0);
    set_flex(row, LV_FLEX_FLOW_ROW, 6);
    lv_obj_t *title = mk_label(row, &font_16s, t.text, task.title.c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(title, 1);
    for (int i = 1; i <= kPlanDays + 1; i++) {
      const bool none = i > kPlanDays;
      String label = none ? String(kNoDate) : i == 1 ? String(kTomorrowBtn) : fmt_date_short(now + i * 86400L).substring(0, 3);
      lv_obj_t *chip = mk_chip(row, label.c_str(), false);
      struct Pick { Task task; String key; };
      Pick *p = attach(chip, Pick{task, none ? String("") : inDays(i)});
      on_click(chip, [](lv_event_t *e) {
        lv_obj_t *chip = lv_event_get_target(e);
        lv_obj_t *row = lv_obj_get_parent(chip);
        for (uint32_t k = 1; k < lv_obj_get_child_cnt(row); k++) set_chip(lv_obj_get_child(row, k), lv_obj_get_child(row, k) == chip);
        Pick *p = static_cast<Pick *>(lv_event_get_user_data(e));
        moveTask(p->task, p->key);
      }, p);
    }
  }
  footerButton(kFinish, true, finish);
}

void buildReview() {
  if (!layer) return;
  lv_obj_clean(layer);
  if (pending.empty()) {
    frame(kReviewTitle, kReviewNothing);
    footerButton(kClose, true, finish);
    return;
  }
  const uint8_t l = g_settings.layout(Layout::Review);
  if (l == 0) reviewChecklist();
  else if (l == 1) reviewOneByOne();
  else reviewPlan();
}

bool due(uint16_t at, long &shownDay) {
  const time_t now = ui_now();
  const long dn = day_number(now);
  if (shownDay == dn) return false;
  struct tm tm;
  localtime_r(&now, &tm);
  const int mins = tm.tm_hour * 60 + tm.tm_min;
  if (mins < at || mins > at + kLateMinutes) return false;
  shownDay = dn;
  return true;
}

}  // namespace

void summary_show() {
  close();
  mk_overlay(false, &layer);
  const Day d = today();
  const uint8_t l = g_settings.layout(Layout::Summary);
  if (l == 0) summaryTiles(d);
  else if (l == 1) summaryTimeline(d);
  else summaryBriefing(d);
}

void review_show() {
  close();
  pending = openTasks();
  current = 0;
  mk_overlay(false, &layer);
  buildReview();
}

void daily_tick() {
  if (millis() - lastCheck < kCheckMs || !clock_valid()) return;
  lastCheck = millis();
  if (ui_current() == Screen::Night || overlay_visible()) return;
  if (g_settings.summary && due(g_settings.summaryAt, summaryDay)) {
    lv_disp_trig_activity(nullptr);
    summary_show();
  } else if (g_settings.review && due(g_settings.reviewAt, reviewDay)) {
    // Only worth asking when something is left.
    if (!openTasks().empty()) {
      lv_disp_trig_activity(nullptr);
      review_show();
    }
  }
}
