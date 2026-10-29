// Focus timer (mockup row 2), started by holding a task: A a ring, B a split
// screen with big digits, C only a pill in the header.
#include "sync.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kFocus = "FOCUS";
constexpr const char *kFocusingOn = "FOCUSING ON";
constexpr const char *kPaused = "PAUSED";
constexpr const char *kTimeUp = "TIME IS UP";
constexpr const char *kOfMinutes = "of %d min";
constexpr const char *kHide = "Hide";
constexpr const char *kPause = "Pause";
constexpr const char *kResume = "Resume";
constexpr const char *kPlus5 = "+5 min";
constexpr const char *kDoneBtn = "Done";
constexpr const char *kStop = "Stop";
constexpr const char *kMarkDone = "Mark task done";
constexpr const char *kNotYet = "Not yet";
constexpr const char *kStarted = "Focus timer: %d min. Tap the timer at the top to open it.";
constexpr const char *kFinished = "Done: %s";
constexpr int kExtraMinutes = 5;
constexpr int RING = 290;

// ---- State -------------------------------------------------------------------

bool running = false;   // started and not stopped (may be paused or over)
bool paused = false;
bool over = false;      // time ran out, waiting for an answer
Task task;
time_t endAt = 0;
long pausedLeft = 0;    // seconds left while paused
long total = 0;         // seconds, for the progress
long shownLeft = -1;

lv_obj_t *layer = nullptr;
lv_obj_t *timeLabel, *stateLabel, *arc, *bar, *pauseLabel, *buttons;

// ---- Code --------------------------------------------------------------------

long left() {
  if (paused) return pausedLeft;
  const long s = (long)(endAt - ui_now());
  return s > 0 ? s : 0;
}

String mmss(long s) { return strf("%ld:%02ld", s / 60, s % 60); }

void close() {
  close_overlay(&layer);
  timeLabel = stateLabel = arc = bar = pauseLabel = buttons = nullptr;
}

void stop() {
  running = paused = over = false;
  close();
}

void finishTask() {
  const Task t = task;
  stop();
  sync_complete_task(t);
  toast(strf(kFinished, t.title.c_str()));
}

void addMinutes(int m) {
  if (over) {
    over = false;
    endAt = ui_now();
  }
  if (paused) pausedLeft += m * 60;
  else endAt += m * 60;
  total += m * 60;
  shownLeft = -1;
}

void togglePause() {
  if (paused) {
    endAt = ui_now() + pausedLeft;
    paused = false;
  } else {
    pausedLeft = left();
    paused = true;
  }
  if (pauseLabel) lv_label_set_text(pauseLabel, paused ? kResume : kPause);
  shownLeft = -1;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, bool primary, lv_event_cb_t cb, int w) {
  lv_obj_t *b = mk_text_button(parent, text, primary, w);
  lv_obj_set_height(b, 56);
  on_click(b, cb);
  return b;
}

// The row of controls; different while running and once time is up.
void fillButtons(int w) {
  if (!buttons) return;
  lv_obj_clean(buttons);
  pauseLabel = nullptr;
  if (over) {
    button(buttons, kNotYet, false, [](lv_event_t *) { stop(); }, (w - 20) / 3);
    button(buttons, kPlus5, false, [](lv_event_t *) {
      addMinutes(kExtraMinutes);
      lv_async_call([](void *) { focus_open(); }, nullptr);
    }, (w - 20) / 3);
    button(buttons, kMarkDone, true, [](lv_event_t *) { lv_async_call([](void *) { finishTask(); }, nullptr); },
           (w - 20) / 3);
    return;
  }
  const int bw = (w - 4 * 10) / 5;
  button(buttons, kHide, false, [](lv_event_t *) { close(); }, bw);
  lv_obj_t *p = button(buttons, paused ? kResume : kPause, false, [](lv_event_t *) { togglePause(); }, bw);
  pauseLabel = lv_obj_get_child(p, 0);
  button(buttons, kPlus5, false, [](lv_event_t *) { addMinutes(kExtraMinutes); }, bw);
  button(buttons, kDoneBtn, true, [](lv_event_t *) { lv_async_call([](void *) { finishTask(); }, nullptr); }, bw);
  button(buttons, kStop, false, [](lv_event_t *) { stop(); }, bw);
}

lv_obj_t *transparent(lv_obj_t *parent) {
  lv_obj_t *o = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  return o;
}

lv_obj_t *titleLabel(lv_obj_t *parent, const lv_font_t *font, int w) {
  lv_obj_t *l = mk_label(parent, font, theme().text, task.title.c_str());
  lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  return l;
}

// A: ring with the time inside, the task above, controls below.
void buildRing() {
  const Theme &t = theme();
  lv_obj_t *head = mk_label(layer, &font_14b, t.accent, kFocusingOn);
  lv_obj_set_style_text_letter_space(head, 1, 0);
  lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 18);
  lv_obj_t *title = titleLabel(layer, &font_20b, SCREEN_W - 120);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_height(title, 28);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

  arc = lv_arc_create(layer);
  lv_obj_set_size(arc, RING, RING);
  lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 80);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 1000);
  lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arc, 16, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 16, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, c(t.raised), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, c(t.accent), LV_PART_INDICATOR);

  timeLabel = mk_label(arc, &font_72, t.text, "");
  lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, -10);
  stateLabel = mk_label(arc, &font_14b, t.muted, kFocus);
  lv_obj_set_style_text_letter_space(stateLabel, 2, 0);
  lv_obj_align(stateLabel, LV_ALIGN_CENTER, 0, 48);

  buttons = transparent(layer);
  lv_obj_set_size(buttons, SCREEN_W - 2 * 40, 56);
  lv_obj_align(buttons, LV_ALIGN_BOTTOM_MID, 0, -20);
  set_flex(buttons, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_CENTER);
  fillButtons(SCREEN_W - 2 * 40);
}

// B: big digits on the left, the task and controls on the right.
void buildSplit() {
  const Theme &t = theme();
  const int leftW = 430;
  lv_obj_t *left = mk_box(layer, t.panel, 0);
  lv_obj_set_size(left, leftW, SCREEN_H);
  stateLabel = mk_label(left, &font_14b, t.accent, kFocus);
  lv_obj_set_style_text_letter_space(stateLabel, 2, 0);
  lv_obj_align(stateLabel, LV_ALIGN_CENTER, 0, -80);
  timeLabel = mk_label(left, &font_72, t.text, "");
  lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, -10);
  bar = lv_bar_create(left);
  lv_obj_set_size(bar, leftW - 120, 10);
  lv_obj_align(bar, LV_ALIGN_CENTER, 0, 60);
  lv_bar_set_range(bar, 0, 1000);
  lv_obj_set_style_bg_color(bar, c(t.raised), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, c(t.accent), LV_PART_INDICATOR);
  lv_obj_t *of = mk_label(left, &font_14, t.muted, strf(kOfMinutes, (int)(total / 60)).c_str());
  lv_obj_align(of, LV_ALIGN_CENTER, 0, 90);

  const int rightW = SCREEN_W - leftW - 2 * 30;
  lv_obj_t *right = transparent(layer);
  lv_obj_set_pos(right, leftW + 30, 40);
  lv_obj_set_size(right, rightW, SCREEN_H - 80);
  set_flex(right, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *head = mk_label(right, &font_12, t.muted, kFocusingOn);
  lv_obj_set_style_text_letter_space(head, 1, 0);
  titleLabel(right, &font_26b, rightW);
  if (task.list.length()) mk_label(right, &font_14, t.muted, task.list.c_str());
  lv_obj_t *fill = transparent(right);
  lv_obj_set_flex_grow(fill, 1);

  // Two rows of controls: they do not fit side by side in half the screen.
  buttons = transparent(right);
  lv_obj_set_width(buttons, rightW);
  lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(buttons, 10, 0);
  lv_obj_set_style_pad_column(buttons, 10, 0);
  fillButtons(rightW);
  if (!over) {
    // Bigger buttons in a 3 + 2 grid instead of five narrow ones.
    const int bw3 = (rightW - 20) / 3, bw2 = (rightW - 10) / 2;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(buttons); i++) {
      lv_obj_set_width(lv_obj_get_child(buttons, i), i < 3 ? bw3 : bw2);
    }
  } else {
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_COLUMN);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(buttons); i++) lv_obj_set_width(lv_obj_get_child(buttons, i), rightW);
  }
}

// C: a small card, opened from the pill.
void buildCard() {
  const Theme &t = theme();
  lv_obj_t *card = mk_box(layer, t.card, 24);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);  // taps on the card do not close it
  lv_obj_set_width(card, 620);
  lv_obj_set_style_pad_all(card, 24, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 12, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);
  lv_obj_t *row = transparent(card);
  lv_obj_set_width(row, 572);
  set_flex(row, LV_FLEX_FLOW_ROW, 16, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  timeLabel = mk_label(row, &font_34b, t.accent, "");
  lv_obj_t *col = transparent(row);
  lv_obj_set_flex_grow(col, 1);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  stateLabel = mk_label(col, &font_12, t.muted, kFocus);
  lv_obj_set_style_text_letter_space(stateLabel, 1, 0);
  lv_obj_t *title = titleLabel(col, &font_16s, 400);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  bar = lv_bar_create(card);
  lv_obj_set_size(bar, 572, 8);
  lv_bar_set_range(bar, 0, 1000);
  lv_obj_set_style_bg_color(bar, c(t.raised), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, c(t.accent), LV_PART_INDICATOR);
  buttons = transparent(card);
  lv_obj_set_size(buttons, 572, 56);
  set_flex(buttons, LV_FLEX_FLOW_ROW, 10);
  fillButtons(572);
}

void build() {
  close();
  const uint8_t l = g_settings.layout(Layout::Focus);
  mk_overlay(l == 2, &layer);
  if (l == 2) {
    on_click(layer, [](lv_event_t *e) {
      if (lv_event_get_target(e) == layer) close();  // tap outside the card
    });
  }
  if (l == 0) buildRing();
  else if (l == 1) buildSplit();
  else buildCard();
  shownLeft = -1;
  focus_tick();
}

}  // namespace

bool focus_enabled_for(const Task &) { return g_settings.focus; }

void focus_start(const Task &t) {
  if (!g_settings.focus) return;
  task = t;
  running = true;
  paused = over = false;
  total = g_settings.focusMinutes * 60L;
  endAt = ui_now() + total;
  if (g_settings.layout(Layout::Focus) == 2) toast(strf(kStarted, (int)g_settings.focusMinutes));
  else build();
}

void focus_hold(lv_obj_t *row, const Task &t) {
  if (!g_settings.focus) return;
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(row, c(theme().raised), LV_STATE_PRESSED);
  Task *copy = attach(row, t);
  lv_obj_add_event_cb(row, [](lv_event_t *e) {
    focus_start(*static_cast<Task *>(lv_event_get_user_data(e)));
  }, LV_EVENT_LONG_PRESSED, copy);
}

bool focus_running() { return running; }

void focus_stop() { stop(); }

bool focus_pill(String &time, String &title, float &progress) {
  if (!running || layer) return false;
  time = over ? String(kTimeUp) : mmss(left());
  title = task.title;
  progress = total > 0 ? 1.0f - (float)left() / (float)total : 1.0f;
  return true;
}

void focus_open() {
  if (running) build();
}

void focus_tick() {
  if (!running) return;
  if (!g_settings.focus) {
    stop();
    return;
  }
  const long s = left();
  if (s == 0 && !over && !paused) {
    over = true;
    play(Sound::Done);
    lv_disp_trig_activity(nullptr);  // wake a dimmed screen
    build();
    return;
  }
  if (!layer || s == shownLeft) return;
  shownLeft = s;
  const int permille = total > 0 ? (int)(1000 - s * 1000 / total) : 1000;
  if (timeLabel) lv_label_set_text(timeLabel, over ? "0:00" : mmss(s).c_str());
  if (stateLabel) lv_label_set_text(stateLabel, over ? kTimeUp : paused ? kPaused : kFocus);
  if (arc) lv_arc_set_value(arc, permille);
  if (bar) lv_bar_set_value(bar, permille, LV_ANIM_OFF);
}
