// Task roulette (mockup row "tr"): the display picks your next task.
// A spins a wheel, B shuffles a deck of cards, C pulls a slot machine with a
// task, a time box and a reward. Overdue, due-today and often-moved tasks
// are more likely to come up.
#include <algorithm>

#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kCantDecide = "CAN'T DECIDE?";
constexpr const char *kWheelTitle = "Let the wheel pick your next task";
constexpr const char *kWheelHint = "Overdue and due-today tasks get bigger slices.";
constexpr const char *kYourTask = "YOUR TASK";
constexpr const char *kSpin = "Spin";
constexpr const char *kSpinAgain = "Spin again";
constexpr const char *kStartFocus = "Start focus timer";
constexpr const char *kClose = "Close";
constexpr const char *kShuffle = "Shuffle again";
constexpr const char *kYourCard = "YOUR CARD";
constexpr const char *kCardOf = "Card %d of %d \xC2\xB7 %d reshuffles left";
constexpr const char *kNoReshuffles = "Card %d of %d \xC2\xB7 no reshuffles left. Go!";
constexpr const char *kSlotHead = "TASK \xC2\xB7 TIME \xC2\xB7 REWARD";
constexpr const char *kPull = "Pull again";
constexpr const char *kDeal = "Deal, start now";
constexpr const char *kNoTasks = "No open tasks to pick from. Enjoy the break!";
constexpr const char *kGoDoIt = "Go: %s";
const char *const kTimes[] = {"10 min", "15 min", "20 min", "25 min", "30 min", "45 min"};
const uint8_t kTimeMinutes[] = {10, 15, 20, 25, 30, 45};
const char *const kRewards[] = {"then a walk",   "then a coffee",  "5 min of music", "then a stretch",
                                "then a snack",  "then a chat",    "a window break", "then tea"};
const uint32_t kSliceColors[] = {0xF5A524, 0x6C8CFF, 0x7BD88F, 0xE57CD8, 0x4FD1C5, 0xFF8A65};
constexpr int kWheelSlices = 6;
constexpr int kReshuffles = 2;
constexpr uint32_t kSpinMs = 3200;
constexpr uint32_t kShuffleMs = 1300;
constexpr uint32_t kReelStepMs = 90;
constexpr uint32_t kReelStopMs[] = {900, 1500, 2100};
constexpr int WHEEL = 320;

// ---- State -------------------------------------------------------------------

lv_obj_t *layer = nullptr;
std::vector<Task> pool;       // candidates, with their weights
std::vector<int> weights;
int picked = -1;
int reshuffles = 0;
int pickedTime = 3, pickedReward = 0;
lv_obj_t *wheel, *legend, *resultBox;
int32_t wheelAngle = 0;
lv_obj_t *cardTitle, *cardMeta, *cardFoot, *cardStack;
lv_obj_t *reels[3];
lv_timer_t *timer = nullptr;
uint32_t animStart = 0;

// ---- Code --------------------------------------------------------------------

int weightOf(const Task &t) {
  Tone tone;
  due_text(t.dueKey, tone);
  return (tone == Tone::Bad ? 3 : tone == Tone::Warn ? 2 : 1) + std::min(radar_moves(t), 3);
}

void fillPool(size_t max) {
  pool.clear();
  weights.clear();
  for (const Task &t : g_snap.tasks) {
    if (!t.parent.length()) pool.push_back(t);
  }
  std::stable_sort(pool.begin(), pool.end(), [](const Task &a, const Task &b) { return weightOf(a) > weightOf(b); });
  if (pool.size() > max) pool.resize(max);
  for (const Task &t : pool) weights.push_back(weightOf(t));
}

int pick() {
  int sum = 0;
  for (int w : weights) sum += w;
  if (!sum) return -1;
  int r = (int)random(sum);
  for (size_t i = 0; i < weights.size(); i++) {
    if ((r -= weights[i]) < 0) return (int)i;
  }
  return 0;
}

void stopTimer() {
  if (timer) lv_timer_del(timer);
  timer = nullptr;
}

void close() {
  stopTimer();
  close_overlay(&layer);
}

void startFocus(int minutes) {
  if (picked < 0 || picked >= (int)pool.size()) return;
  const Task t = pool[picked];
  close();
  if (g_settings.focus) focus_start(t, minutes);
  else toast(strf(kGoDoIt, t.title.c_str()));
}

lv_obj_t *transparent(lv_obj_t *parent) {
  lv_obj_t *o = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  return o;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, bool primary, lv_event_cb_t cb) {
  lv_obj_t *b = mk_text_button(parent, text, primary);
  lv_obj_set_height(b, 56);
  lv_obj_set_style_pad_hor(b, 26, 0);
  on_click(b, cb);
  return b;
}

void closeButton(lv_obj_t *parent) {
  button(parent, kClose, false, [](lv_event_t *) { lv_async_call([](void *) { close(); }, nullptr); });
}

void emptyState() {
  const Theme &t = theme();
  lv_obj_t *l = mk_label(layer, &font_20b, t.muted, kNoTasks);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, -30);
  lv_obj_t *row = transparent(layer);
  lv_obj_set_size(row, LV_SIZE_CONTENT, 56);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, 40);
  closeButton(row);
}

// ---- A: wheel ----

// Slices as thick arcs; spinning turns them all together.
void setWheelAngle(void *, int32_t v) {
  wheelAngle = v;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(wheel); i++) {
    lv_obj_t *arc = lv_obj_get_child(wheel, i);
    if (lv_obj_check_type(arc, &lv_arc_class)) lv_arc_set_rotation(arc, (uint16_t)(v % 360));
  }
}

void spinLater(lv_event_t *);

void showWheelResult() {
  const Theme &t = theme();
  lv_obj_clean(resultBox);
  lv_obj_t *head = mk_label(resultBox, &font_12, kSliceColors[picked % 6], kYourTask);
  lv_obj_set_style_text_letter_space(head, 1, 0);
  lv_obj_t *title = mk_label(resultBox, &font_26b, t.text, pool[picked].title.c_str());
  lv_obj_set_width(title, 380);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  if (pool[picked].list.length()) mk_label(resultBox, &font_14, t.muted, pool[picked].list.c_str());
  lv_obj_t *row = transparent(resultBox);
  lv_obj_set_size(row, 380, 56);
  lv_obj_set_style_pad_top(row, 0, 0);
  set_flex(row, LV_FLEX_FLOW_ROW, 10);
  button(row, g_settings.focus ? kStartFocus : kClose, true, [](lv_event_t *) {
    lv_async_call([](void *) { startFocus(0); }, nullptr);
  });
  button(row, kSpinAgain, false, spinLater);
}

void wheelIntro() {
  const Theme &t = theme();
  lv_obj_clean(resultBox);
  lv_obj_t *head = mk_label(resultBox, &font_12, t.muted, kCantDecide);
  lv_obj_set_style_text_letter_space(head, 1, 0);
  lv_obj_t *title = mk_label(resultBox, &font_26b, t.text, kWheelTitle);
  lv_obj_set_width(title, 380);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  mk_label(resultBox, &font_14, t.muted, kWheelHint);
  lv_obj_t *row = transparent(resultBox);
  lv_obj_set_size(row, 380, 56);
  set_flex(row, LV_FLEX_FLOW_ROW, 10);
  button(row, kSpin, true, spinLater);
  closeButton(row);
}

void spin() {
  if (lv_anim_get(wheel, nullptr)) return;  // still turning
  picked = pick();
  if (picked < 0) return;
  lv_obj_clean(resultBox);
  // Slice i covers [start, end) before rotation; the pointer is at the top (270).
  int sum = 0, before = 0;
  for (size_t i = 0; i < weights.size(); i++) sum += weights[i];
  for (int i = 0; i < picked; i++) before += weights[i];
  const int mid = (int)((before + weights[picked] / 2.0f) * 360 / sum);
  const int jitter = (int)random(-(weights[picked] * 120 / sum), weights[picked] * 120 / sum + 1);
  int target = 270 - mid + jitter;
  while (target < wheelAngle % 360) target += 360;
  target += 360 * 4 + (wheelAngle / 360) * 360;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, wheel);
  lv_anim_set_exec_cb(&a, [](void *v, int32_t x) { setWheelAngle(v, x); });
  lv_anim_set_values(&a, wheelAngle, target);
  lv_anim_set_time(&a, kSpinMs);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_ready_cb(&a, [](lv_anim_t *) { showWheelResult(); });
  lv_anim_start(&a);
}

// The buttons live in the box that spinning clears, so spin after the click.
void spinLater(lv_event_t *) {
  lv_async_call([](void *) { spin(); }, nullptr);
}

void buildWheel() {
  const Theme &t = theme();
  fillPool(kWheelSlices);
  if (pool.empty()) return emptyState();
  wheel = transparent(layer);
  lv_obj_set_size(wheel, WHEEL, WHEEL);
  lv_obj_align(wheel, LV_ALIGN_LEFT_MID, 40, 16);
  lv_obj_clear_flag(wheel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(wheel, [](lv_event_t *e) { lv_anim_del(lv_event_get_target(e), nullptr); }, LV_EVENT_DELETE,
                      nullptr);
  int sum = 0;
  for (int w : weights) sum += w;
  int from = 0;
  for (size_t i = 0; i < pool.size(); i++) {
    const int a0 = from * 360 / sum, a1 = (from + weights[i]) * 360 / sum;
    from += weights[i];
    lv_obj_t *arc = lv_arc_create(wheel);
    lv_obj_set_size(arc, WHEEL, WHEEL);
    lv_obj_center(arc);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, WHEEL / 2 - 40, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, c(kSliceColors[i % 6]), LV_PART_INDICATOR);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, a0, a1 - 1);  // a thin gap between slices
    lv_arc_set_rotation(arc, (uint16_t)(wheelAngle % 360));
  }
  lv_obj_t *hub = mk_box(wheel, t.bg, 40);
  lv_obj_set_size(hub, 80, 80);
  lv_obj_center(hub);
  lv_obj_t *pointer = mk_box(layer, t.text, 4);  // marks the winning slice at the top
  lv_obj_set_size(pointer, 8, 34);
  lv_obj_align_to(pointer, wheel, LV_ALIGN_OUT_TOP_MID, 0, 22);

  legend = transparent(layer);
  lv_obj_set_pos(legend, 40 + WHEEL + 40, 24);
  lv_obj_set_size(legend, SCREEN_W - (40 + WHEEL + 40) - 24, 150);
  set_flex(legend, LV_FLEX_FLOW_COLUMN, 4, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  for (size_t i = 0; i < pool.size(); i++) {
    lv_obj_t *row = transparent(legend);
    set_flex(row, LV_FLEX_FLOW_ROW, 8);
    mk_dot(row, kSliceColors[i % 6], 10);
    lv_obj_t *l = mk_label(row, &font_12, t.muted, pool[i].title.c_str());
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, 340);
  }
  resultBox = transparent(layer);
  lv_obj_set_pos(resultBox, 40 + WHEEL + 40, 190);
  lv_obj_set_size(resultBox, SCREEN_W - (40 + WHEEL + 40) - 24, SCREEN_H - 210);
  set_flex(resultBox, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  wheelIntro();
}

// ---- B: deck of cards ----

void showCard(int i, bool final) {
  const Theme &t = theme();
  lv_label_set_text(cardTitle, pool[i].title.c_str());
  lv_label_set_text(cardMeta, pool[i].list.c_str());
  lv_obj_set_style_border_color(lv_obj_get_child(cardStack, 2), c(final ? t.accent : t.line), 0);
  if (final) {
    lv_label_set_text(cardFoot, (reshuffles ? strf(kCardOf, i + 1, (int)pool.size(), reshuffles)
                                            : strf(kNoReshuffles, i + 1, (int)pool.size())).c_str());
  } else {
    lv_label_set_text(cardFoot, "");
  }
}

void shuffle() {
  stopTimer();
  picked = pick();
  if (picked < 0) return;
  animStart = millis();
  timer = lv_timer_create([](lv_timer_t *) {
    const uint32_t ms = millis() - animStart;
    lv_obj_t *top = lv_obj_get_child(cardStack, 2);
    if (ms >= kShuffleMs) {
      stopTimer();
      lv_obj_set_x(top, 0);
      showCard(picked, true);
      return;
    }
    // The top card slides out and back with a new task each time.
    const int phase = (ms / 130) % 2;
    lv_obj_set_x(top, phase ? 24 : -24);
    showCard((int)random(pool.size()), false);
  }, 65, nullptr);
}

void buildDeck() {
  const Theme &t = theme();
  fillPool(12);
  if (pool.empty()) return emptyState();
  reshuffles = kReshuffles;
  cardStack = transparent(layer);
  lv_obj_set_size(cardStack, 460, 240);
  lv_obj_align(cardStack, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_clear_flag(cardStack, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(cardStack, 12, 0);
  const uint32_t backs[] = {t.raised, t.panel};
  const int offsets[][2] = {{-14, 10}, {12, 5}};
  for (int i = 0; i < 2; i++) {
    lv_obj_t *back = mk_box(cardStack, backs[i], 22);
    lv_obj_set_size(back, 420, 210);
    lv_obj_align(back, LV_ALIGN_CENTER, offsets[i][0], offsets[i][1]);
  }
  lv_obj_t *top = mk_box(cardStack, t.card, 22);
  lv_obj_set_size(top, 420, 210);
  lv_obj_align(top, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_border_width(top, 2, 0);
  lv_obj_set_style_pad_all(top, 24, 0);
  set_flex(top, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *head = mk_label(top, &font_12, t.accent, kYourCard);
  lv_obj_set_style_text_letter_space(head, 1, 0);
  cardTitle = mk_label(top, &font_26b, t.text, "");
  lv_obj_set_width(cardTitle, 370);
  lv_label_set_long_mode(cardTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_height(cardTitle, 70);
  cardMeta = mk_label(top, &font_14, t.muted, "");
  cardFoot = mk_label(top, &font_12, t.muted, "");

  lv_obj_t *row = transparent(layer);
  lv_obj_set_size(row, LV_SIZE_CONTENT, 56);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -40);
  set_flex(row, LV_FLEX_FLOW_ROW, 10);
  button(row, kShuffle, false, [](lv_event_t *e) {
    if (timer) return;
    if (reshuffles <= 0) {
      lv_obj_add_state(lv_event_get_target(e), LV_STATE_DISABLED);
      return;
    }
    reshuffles--;
    shuffle();
  });
  button(row, g_settings.focus ? kStartFocus : kClose, true, [](lv_event_t *) {
    if (!timer) lv_async_call([](void *) { startFocus(0); }, nullptr);
  });
  closeButton(row);
  shuffle();
}

// ---- C: slot machine ----

lv_obj_t *reel(lv_obj_t *parent) {
  const Theme &t = theme();
  lv_obj_t *r = mk_box(parent, t.dark ? 0x0B0E11 : t.panel, 16);
  lv_obj_set_size(r, 220, 210);
  lv_obj_set_style_border_color(r, c(t.line), 0);
  lv_obj_set_style_border_width(r, 2, 0);
  set_flex(r, LV_FLEX_FLOW_COLUMN, 12, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int i = 0; i < 3; i++) {
    lv_obj_t *l = mk_label(r, i == 1 ? &font_16s : &font_14, i == 1 ? t.text : t.line, "");
    lv_obj_set_width(l, 200);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    if (i == 1) {
      lv_obj_set_style_pad_ver(l, 10, 0);
      lv_obj_set_style_border_side(l, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, 0);
      lv_obj_set_style_border_color(l, c(t.accent), 0);
      lv_obj_set_style_border_width(l, 2, 0);
    }
  }
  return r;
}

const char *reelText(int reel, int i) {
  switch (reel) {
    case 0: return pool[(i + pool.size()) % pool.size()].title.c_str();
    case 1: return kTimes[(i + 6) % 6];
    default: return kRewards[(i + 8) % 8];
  }
}

void setReel(int r, int i) {
  for (int k = 0; k < 3; k++) lv_label_set_text(lv_obj_get_child(reels[r], k), reelText(r, i - 1 + k));
}

void pull() {
  stopTimer();
  picked = pick();
  if (picked < 0) return;
  pickedTime = (int)random(6);
  pickedReward = (int)random(8);
  animStart = millis();
  timer = lv_timer_create([](lv_timer_t *) {
    const uint32_t ms = millis() - animStart;
    const int step = ms / kReelStepMs;
    const int finals[] = {picked, pickedTime, pickedReward};
    for (int r = 0; r < 3; r++) setReel(r, ms >= kReelStopMs[r] ? finals[r] : step + r * 2);
    if (ms >= kReelStopMs[2]) {
      stopTimer();
      play(Sound::Done);
    }
  }, 45, nullptr);
}

void buildSlots() {
  const Theme &t = theme();
  fillPool(12);
  if (pool.empty()) return emptyState();
  lv_obj_t *head = mk_label(layer, &font_14b, t.accent, kSlotHead);
  lv_obj_set_style_text_letter_space(head, 2, 0);
  lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_t *machine = mk_box(layer, t.card, 22);
  lv_obj_set_size(machine, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(machine, 16, 0);
  set_flex(machine, LV_FLEX_FLOW_ROW, 14);
  lv_obj_align(machine, LV_ALIGN_TOP_MID, 0, 74);
  for (int r = 0; r < 3; r++) reels[r] = reel(machine);
  lv_obj_t *row = transparent(layer);
  lv_obj_set_size(row, LV_SIZE_CONTENT, 56);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -40);
  set_flex(row, LV_FLEX_FLOW_ROW, 10);
  button(row, kPull, false, [](lv_event_t *) {
    if (!timer) pull();
  });
  button(row, kDeal, true, [](lv_event_t *) {
    if (!timer) lv_async_call([](void *) { startFocus(kTimeMinutes[pickedTime]); }, nullptr);
  });
  closeButton(row);
  pull();
}

}  // namespace

void roulette_show() {
  if (!g_settings.roulette) return;
  close();
  picked = -1;
  mk_overlay(false, &layer);
  lv_obj_add_event_cb(layer, [](lv_event_t *) { stopTimer(); }, LV_EVENT_DELETE, nullptr);
  switch (g_settings.layout(Layout::Roulette)) {
    case 1: buildDeck(); break;
    case 2: buildSlots(); break;
    default: buildWheel(); break;
  }
}
