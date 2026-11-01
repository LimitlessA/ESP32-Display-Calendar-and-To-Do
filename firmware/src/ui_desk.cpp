// Smart standing desk (mockup row "sd"): A a dock along the bottom of Home,
// B a Desk screen with the height, memories and the sit/stand timer, C a Desk
// screen where the desk follows your calendar: it stands up for meetings and
// focus timers and goes to away mode when your phone leaves.
#include "desk.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kCm = "%d.%d cm";
constexpr const char *kCmShort = "%d cm";
constexpr const char *kSit = "Sit";
constexpr const char *kStand = "Stand";
constexpr const char *kSlot = "M%d";
constexpr const char *kSlotRole = "M%d \xC2\xB7 %s";
constexpr const char *kSitting = "Sitting";
constexpr const char *kStanding = "Standing";
constexpr const char *kMoving = "Moving";
constexpr const char *kStopped = "Stopped";
constexpr const char *kOffline = "Desk offline";
constexpr const char *kDeskWord = "desk";
constexpr const char *kStandIn = "Stand in %ld min";
constexpr const char *kSitIn = "Sit in %ld min";
constexpr const char *kTimerOff = "Timer off";
constexpr const char *kTimerPaused = "Timer paused";
constexpr const char *kStop = "Stop";
constexpr const char *kMemories = "MEMORY POSITIONS";
constexpr const char *kRaise = "Hold to raise";
constexpr const char *kLower = "Hold to lower";
constexpr const char *kNeedsLogin = "Add the desk login in Desk settings to raise, lower and run the timer.";
constexpr const char *kDeskSettings = "Desk settings";
constexpr int kTimerMin = 5, kTimerMax = 90, kTimerStep = 5, kTimerDefault = 30;
constexpr const char *kMin = "min";
constexpr const char *kThen = "then %s %d";
constexpr const char *kNext = "%s %d next";
constexpr const char *kClock = "%d:%02d";
constexpr const char *kToday = "Today: %s standing \xC2\xB7 %d changes";
constexpr const char *kStateFor = "%s \xC2\xB7 %s";
constexpr const char *kTodayAtDesk = "TODAY AT THE DESK";
constexpr const char *kStood = "stood";
constexpr const char *kSat = "sat";
constexpr const char *kPlanned = "planned: stand for meetings";
constexpr const char *kRules = "RULES";
constexpr const char *kRuleMeetings = "Stand for meetings of 15 min or more";
constexpr const char *kRuleFocus = "Stand during focus timers";
constexpr const char *kRuleAway = "Away mode when your phone leaves";
constexpr const char *kInMin = "IN %ld MIN \xC2\xB7 %s";
constexpr const char *kNowIn = "NOW \xC2\xB7 %s";
constexpr const char *kStandingFor = "Standing up for this meeting";
constexpr const char *kStandPlan = "The desk goes to M%d at %s, and back to M%d when it ends.";
constexpr const char *kStaySeated = "Stay seated";
constexpr const char *kStandNow = "Stand now";
constexpr const char *kNoPlan = "No meetings to stand for";
constexpr const char *kNoPlanSub = "Use the buttons, or turn on a rule.";
constexpr const char *kHoursMin = "%d h %02d min";
constexpr const char *kMinutes = "%d min";
constexpr int kStandingMm = 1000;     // the desk calls 1000 mm and up standing
constexpr int kMeetingMinMinutes = 15;
constexpr long kPromptBefore = 60;    // seconds before the meeting: "standing up" popup
constexpr int kDayFrom = 7, kDayTo = 19;  // C: timeline hours
constexpr int DOCK_H = 64;
constexpr int BODY_Y = HEADER_H + GAP;
constexpr int BODY_H = SCREEN_H - BODY_Y - PAD;

// ---- State -------------------------------------------------------------------

// Labels kept up to date every second, for whichever view is on screen.
struct Live {
  lv_obj_t *height = nullptr, *state = nullptr, *timer = nullptr, *timerBar = nullptr, *stop = nullptr;
  lv_obj_t *card = nullptr, *ring = nullptr, *ringNum = nullptr;  // B: the timer card
  int sig = 0, shown = -1;
  lv_obj_t *mem[4] = {};
  lv_obj_t *deskTop = nullptr;   // B: the drawn desk top moves with the height...
  lv_obj_t *legs[2] = {};        // ...and the legs grow under it
  lv_obj_t *today = nullptr;
  int curMm = 0;                 // height the drawing is at right now
  int shownMm = 0;               // height the drawing is at (or gliding to)
};
Live dockLive, screenLive;
lv_obj_t *deskBody = nullptr;
lv_obj_t *prompt = nullptr;
uint32_t seenVersion = 0, lastTick = 0;

// Today's observed positions in 5-minute slots: 0 unknown, 1 sat, 2 stood.
uint8_t slots[24 * 12];
long slotsDay = -1;
int standMinutesToday = 0, changesToday = 0, lastMinuteSampled = -1;
bool wasStanding = false;
time_t stateSince = 0;
String lastState;

// Automation (C)
String raisedFor;        // meeting key we stood up for
String skipped;          // meeting key "Stay seated" was pressed for
String promptedFor;
bool focusRaised = false, wasFocus = false;
int awaySent = -1;       // last away state sent to the desk

// ---- Code --------------------------------------------------------------------

bool on(int layout) { return g_settings.desk && g_settings.layout(Layout::Desk) == layout; }

String cm(int mm, bool decimal) {
  if (mm <= 0) return "--";
  return decimal ? strf(kCm, mm / 10, mm % 10) : strf(kCmShort, (mm + 5) / 10);
}

String duration(long secs) {
  const int m = (int)(secs / 60);
  return m >= 60 ? strf(kHoursMin, m / 60, m % 60) : strf(kMinutes, m);
}

String slotName(uint8_t slot) {
  // Always the desk's own name (M1-M4); sit/stand is only a hint beside it.
  if (slot == g_settings.deskSitSlot) return strf(kSlotRole, slot, kSit);
  if (slot == g_settings.deskStandSlot) return strf(kSlotRole, slot, kStand);
  return strf(kSlot, slot);
}

String stateText(const DeskStatus &st) {
  if (!st.online) return kOffline;
  if (st.moving) return kMoving;
  if (st.state == "EMERGENCY_STOP") return kStopped;
  return st.heightMm >= kStandingMm ? kStanding : kSitting;
}

String timerText(const DeskStatus &st) {
  if (!st.timerRunning) return kTimerOff;
  if (st.timerPaused) return kTimerPaused;
  const long m = (st.timerLeft + 59) / 60;
  return strf(st.timerPhase == "SIT" ? kStandIn : kSitIn, m);
}

int timerProgress(const DeskStatus &st) {  // 0-1000
  const long len = (st.timerPhase == "SIT" ? st.sitMinutes : st.standMinutes) * 60L;
  if (!st.timerRunning || len <= 0) return 0;
  return (int)(1000 - std::min(st.timerLeft, len) * 1000 / len);
}

// The drawn desk: 650-1300 mm maps onto 150 px of travel. Only the top and the two thin legs move.
void placeDesk(Live &live, int mm) {
  live.curMm = mm;
  const int y = 190 - (std::max(650, std::min(1300, mm)) - 650) * 150 / 650;
  if (lv_obj_get_y(live.deskTop) == y) return;
  lv_obj_set_y(live.deskTop, y);
  for (lv_obj_t *leg : live.legs) {
    lv_obj_set_y(leg, y + 14);
    lv_obj_set_height(leg, 256 - y - 14);
  }
}

void deskGlide(void *var, int32_t mm) { placeDesk(*static_cast<Live *>(var), mm); }

void forget(Live *live) {
  lv_anim_del(live, deskGlide);
  *live = Live();
}

lv_obj_t *transparent(lv_obj_t *parent) {
  lv_obj_t *o = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  return o;
}

// Memory button: name on top, height under it; lit when the desk is there.
lv_obj_t *memButton(lv_obj_t *parent, uint8_t slot, Live &live, int w, int h) {
  const Theme &t = theme();
  lv_obj_t *b = mk_button(parent, t.raised, 14);
  lv_obj_set_size(b, w, h);
  set_flex(b, LV_FLEX_FLOW_COLUMN, 0, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  mk_label(b, &font_14b, t.text, slotName(slot).c_str());
  mk_label(b, &font_12, t.muted, "");
  on_click(b, [](lv_event_t *e) { desk_recall((uint8_t)(intptr_t)lv_event_get_user_data(e)); }, (void *)(intptr_t)slot);
  live.mem[slot - 1] = b;
  return b;
}

// Raise/lower while the finger is down, stop on release.
void holdButton(lv_obj_t *b, int dir) {
  lv_obj_add_event_cb(b, [](lv_event_t *e) { desk_hold((int)(intptr_t)lv_event_get_user_data(e)); }, LV_EVENT_PRESSED,
                      (void *)(intptr_t)dir);
  lv_obj_add_event_cb(b, [](lv_event_t *) { desk_hold(0); }, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(b, [](lv_event_t *) { desk_hold(0); }, LV_EVENT_PRESS_LOST, nullptr);
}

lv_obj_t *roundButton(lv_obj_t *parent, const char *icon, int size) {
  const Theme &t = theme();
  lv_obj_t *b = mk_button(parent, t.raised, size / 2);
  lv_obj_set_size(b, size, size);
  lv_obj_center(mk_label(b, &font_20b, t.text, icon));
  return b;
}

void updateTimerCard(Live &live, const DeskStatus &st);

void updateLive(Live &live, const DeskStatus &st) {
  const Theme &t = theme();
  if (live.height) lv_label_set_text(live.height, cm(st.heightMm, true).c_str());
  if (live.state) {
    String s = stateText(st);
    if (stateSince && st.online && !st.moving) s = strf(kStateFor, s.c_str(), duration(ui_now() - stateSince).c_str());
    lv_label_set_text(live.state, s.c_str());
    lv_obj_set_style_text_color(live.state, c(!st.online ? t.badFg : st.heightMm >= kStandingMm ? t.good : t.muted), 0);
  }
  updateTimerCard(live, st);
  if (live.timer) lv_label_set_text(live.timer, timerText(st).c_str());
  if (live.timerBar) lv_bar_set_value(live.timerBar, timerProgress(st), LV_ANIM_OFF);
  if (live.stop) {
    if (st.moving) lv_obj_clear_flag(live.stop, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(live.stop, LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < 4; i++) {
    if (!live.mem[i]) continue;
    const int mm = st.memoryMm[i];
    const bool here = mm > 0 && !st.moving && abs(st.heightMm - mm) <= 10;
    lv_obj_set_style_bg_color(live.mem[i], c(here ? t.accent : t.raised), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(live.mem[i], 0), c(here ? t.onAccent : t.text), 0);
    lv_obj_t *sub = lv_obj_get_child(live.mem[i], 1);
    lv_obj_set_style_text_color(sub, c(here ? t.onAccent : t.muted), 0);
    lv_label_set_text(sub, mm > 0 ? cm(mm, false).c_str() : "");
  }
  if (live.deskTop) {
    // Glide from the height shown to the one reported, easing out, instead of jumping per poll.
    const int mm = st.heightMm ? st.heightMm : 750;
    if (!live.shownMm) {
      live.shownMm = mm;
      placeDesk(live, mm);
    } else if (mm != live.shownMm) {
      lv_anim_t a;
      lv_anim_init(&a);
      lv_anim_set_var(&a, &live);
      lv_anim_set_exec_cb(&a, deskGlide);
      lv_anim_set_values(&a, live.curMm, mm);
      lv_anim_set_time(&a, 900);
      lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
      lv_anim_del(&live, deskGlide);
      lv_anim_start(&a);
      live.shownMm = mm;
    }
  }
  if (live.today) {
    lv_label_set_text(live.today, strf(kToday, duration(standMinutesToday * 60L).c_str(), changesToday).c_str());
  }
}

// Timer lengths: what was set here, else what the desk reports, else a default.
int timerLength(bool stand, const DeskStatus &st) {
  int v = stand ? g_settings.deskStandMin : g_settings.deskSitMin;
  if (!v) v = stand ? st.standMinutes : st.sitMinutes;
  if (v <= 0) v = kTimerDefault;
  return std::max(kTimerMin, std::min(kTimerMax, v));
}


void setTimerLength(int v, void *user) {
  Settings s = g_settings;
  if (user) s.deskStandMin = (uint8_t)v;
  else s.deskSitMin = (uint8_t)v;
  // Keeps the other length fixed too, so both are sent together.
  if (!s.deskSitMin) s.deskSitMin = (uint8_t)timerLength(false, desk_status());
  if (!s.deskStandMin) s.deskStandMin = (uint8_t)timerLength(true, desk_status());
  ui_save_settings(s);
  desk_timer_apply();
}

// ---- Timer card: a ring with the minutes left, the phase beside it ----

int timerSig(const DeskStatus &st) {  // what the card looks like; it is rebuilt only when this changes
  if (!st.timerRunning) return 0;
  return 1 + (st.timerPaused ? 1 : 0) + (st.timerPhase == "SIT" ? 2 : 0) + (st.timerLeft <= 60 ? 4 : 0);
}

String numText(int v) { return String(v); }

void timerPress(void *cmd) {
  desk_timer_cmd((int)(intptr_t)cmd);
  lv_async_call([](void *) { desk_refresh(); }, nullptr);
}

void timerButton(lv_obj_t *parent, uint32_t bg, uint32_t fg, const char *icon, int w, int cmd) {
  lv_obj_t *b = mk_button(parent, bg, 14);
  lv_obj_set_size(b, w, 44);
  if (w == 0) lv_obj_set_flex_grow(b, 1);
  lv_obj_center(mk_label(b, &font_16, fg, icon));
  on_click(b, [](lv_event_t *e) { timerPress(lv_event_get_user_data(e)); }, (void *)(intptr_t)cmd);
}

void buildTimerCard(const DeskStatus &st) {
  const Theme &t = theme();
  Live &live = screenLive;
  lv_obj_clean(live.card);
  live.sig = timerSig(st);
  live.shown = -1;
  const bool run = st.timerRunning, sit = st.timerPhase == "SIT", last = run && st.timerLeft <= 60;
  const uint32_t col = last ? t.warnFg : sit ? t.accent : t.good;
  lv_obj_t *wrap = transparent(live.card);
  lv_obj_set_size(wrap, 100, 100);
  lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *ring = lv_arc_create(wrap);
  lv_obj_set_size(ring, 100, 100);
  lv_obj_remove_style(ring, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  lv_arc_set_rotation(ring, 270);
  lv_arc_set_bg_angles(ring, 0, 360);
  lv_arc_set_range(ring, 0, 1000);
  lv_obj_set_style_arc_width(ring, 9, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring, 9, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring, c(t.line), LV_PART_MAIN);
  lv_obj_set_style_arc_color(ring, c(col), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);
  if (!run) lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_INDICATOR);
  live.ring = ring;
  lv_obj_t *side = transparent(live.card);
  lv_obj_set_height(side, 100);
  lv_obj_set_flex_grow(side, 1);
  if (!run) {
    // A play triangle, drawn (the icon font is too small here).
    lv_obj_add_event_cb(wrap, [](lv_event_t *e) {
      lv_obj_t *o = lv_event_get_target(e);
      lv_area_t a;
      lv_obj_get_coords(o, &a);
      const lv_point_t pts[3] = {{(lv_coord_t)(a.x1 + 38), (lv_coord_t)(a.y1 + 28)},
                                 {(lv_coord_t)(a.x1 + 38), (lv_coord_t)(a.y1 + 72)},
                                 {(lv_coord_t)(a.x1 + 76), (lv_coord_t)(a.y1 + 50)}};
      lv_draw_rect_dsc_t d;
      lv_draw_rect_dsc_init(&d);
      d.bg_color = c(theme().accent);
      lv_draw_polygon(lv_event_get_draw_ctx(e), &d, pts, 3);
    }, LV_EVENT_DRAW_MAIN_END, nullptr);
    on_click(wrap, [](lv_event_t *) { timerPress((void *)1); });
    set_flex(side, LV_FLEX_FLOW_COLUMN, 4, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    for (int stand = 0; stand < 2; stand++) {
      lv_obj_t *r = transparent(side);
      lv_obj_set_size(r, LV_SIZE_CONTENT, TOUCH);
      set_flex(r, LV_FLEX_FLOW_ROW, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_width(mk_label(r, &font_14b, t.muted, stand ? kStand : kSit), 50);
      mk_stepper(r, timerLength(stand, st), kTimerMin, kTimerMax, kTimerStep, numText, setTimerLength,
                 (void *)(intptr_t)stand, 44);
    }
    return;
  }
  lv_obj_t *num = transparent(wrap);
  lv_obj_set_size(num, 80, LV_SIZE_CONTENT);
  set_flex(num, LV_FLEX_FLOW_COLUMN, 0, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_center(num);
  live.ringNum = mk_label(num, last ? &font_26b : &font_34b, last ? t.warnFg : t.text, "");
  if (!last) mk_label(num, &font_12, t.muted, kMin);
  set_flex(side, LV_FLEX_FLOW_COLUMN, 3, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *head = transparent(side);
  lv_obj_set_size(head, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  set_flex(head, LV_FLEX_FLOW_ROW, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  mk_dot(head, col, 12);
  mk_label(head, &font_20b, col, sit ? kSitting : kStanding);
  const char *next = sit ? kStand : kSit;
  const int nextMin = timerLength(sit, st);
  mk_label(side, &font_14, t.muted, strf(last ? kNext : kThen, next, nextMin).c_str());
  lv_obj_t *btns = transparent(side);
  lv_obj_set_size(btns, lv_pct(100), 44);
  set_flex(btns, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  timerButton(btns, t.accent, t.onAccent, st.timerPaused ? ICON_PLAY : ICON_PAUSE, 0, st.timerPaused ? 3 : 2);
  timerButton(btns, t.badBg, t.badFg, ICON_STOP, 56, 0);
}

// Once a second: the arc and the number (m:ss in the last minute). Rebuilds the card when its look changes.
void updateTimerCard(Live &live, const DeskStatus &st) {
  if (!live.card) return;
  if (timerSig(st) != live.sig) buildTimerCard(st);
  if (!st.timerRunning || !live.ringNum) return;
  const bool last = st.timerLeft <= 60;
  const int shown = last ? (int)st.timerLeft : (int)((st.timerLeft + 59) / 60);
  if (shown == live.shown) return;
  live.shown = shown;
  lv_label_set_text(live.ringNum, (last ? strf(kClock, shown / 60, shown % 60) : String(shown)).c_str());
  const long len = (st.timerPhase == "SIT" ? st.sitMinutes : st.standMinutes) * 60L;
  lv_arc_set_value(live.ring, last ? (int)(st.timerLeft * 1000 / 60) : len > 0 ? (int)(std::min(st.timerLeft, len) * 1000 / len) : 0);
}

// ---- B: Desk screen ----

void buildScreenB(const DeskStatus &st) {
  const Theme &t = theme();
  Live &live = screenLive;
  lv_obj_t *left = mk_box(deskBody, t.panel, 18);
  lv_obj_set_size(left, 300, BODY_H);
  lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *cfg = mk_chip(left, kDeskSettings, false, 32);
  lv_obj_set_pos(cfg, 10, 8);
  on_click(cfg, [](lv_event_t *) { lv_async_call([](void *) { desk_settings_show(); }, nullptr); });
  // A drawn desk: the top moves up and down with the real one.
  for (int i = 0; i < 2; i++) {
    live.legs[i] = mk_box(left, t.line, 2);
    lv_obj_set_size(live.legs[i], 12, 100);
    lv_obj_set_x(live.legs[i], i ? 196 : 80);
  }
  for (int x : {64, 180}) {
    lv_obj_t *foot = mk_box(left, t.line, 4);
    lv_obj_set_size(foot, 44, 8);
    lv_obj_set_pos(foot, x, 256);
  }
  live.deskTop = mk_box(left, t.accent, 6);
  lv_obj_set_size(live.deskTop, 200, 14);
  lv_obj_set_x(live.deskTop, 50);
  lv_obj_t *col = transparent(left);
  lv_obj_set_width(col, 300);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(col, LV_ALIGN_BOTTOM_MID, 0, -20);
  live.height = mk_label(col, &font_34b, t.text, "");
  live.state = mk_label(col, &font_14b, t.good, "");
  lv_obj_set_style_text_letter_space(live.state, 1, 0);

  const int rw = SCREEN_W - 2 * PAD - 300 - GAP;
  lv_obj_t *right = transparent(deskBody);
  lv_obj_set_size(right, rw, BODY_H);
  set_flex(right, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *h = mk_label(right, &font_12, t.muted, kMemories);
  lv_obj_set_style_text_letter_space(h, 1, 0);
  lv_obj_t *grid = transparent(right);
  lv_obj_set_width(grid, rw);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, 10, 0);
  lv_obj_set_style_pad_column(grid, 10, 0);
  for (uint8_t i = 1; i <= 4; i++) memButton(grid, i, live, (rw - 10) / 2, 56);

  lv_obj_t *row = transparent(right);
  lv_obj_set_size(row, rw, 56);
  set_flex(row, LV_FLEX_FLOW_ROW, 10);
  if (st.hasLogin) {
    for (int dir : {1, -1}) {
      lv_obj_t *b = mk_button(row, t.raised, 14);
      lv_obj_set_size(b, (rw - 120) / 2, 56);
      set_flex(b, LV_FLEX_FLOW_ROW, 8, LV_FLEX_ALIGN_CENTER);
      mk_label(b, &font_16, t.text, dir > 0 ? ICON_CARET_UP : ICON_CARET_DN);
      mk_label(b, &font_14b, t.text, dir > 0 ? kRaise : kLower);
      holdButton(b, dir);
    }
  } else {
    lv_obj_t *hint = mk_label(row, &font_12, t.muted, kNeedsLogin);
    lv_obj_set_width(hint, rw - 120);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  }
  lv_obj_t *stop = mk_button(row, t.badBg, 14);
  lv_obj_set_size(stop, 100, 56);
  lv_obj_center(mk_label(stop, &font_14b, t.badFg, kStop));
  on_click(stop, [](lv_event_t *) { desk_stop(); });

  if (st.hasLogin) {  // the timer needs the desk login
    live.card = mk_box(right, t.card, 14);
    lv_obj_set_size(live.card, rw, 120);
    lv_obj_set_style_pad_all(live.card, 10, 0);
    lv_obj_clear_flag(live.card, LV_OBJ_FLAG_SCROLLABLE);
    set_flex(live.card, LV_FLEX_FLOW_ROW, 14, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    buildTimerCard(st);
  }
  live.today = mk_label(right, &font_12, t.muted, "");
  lv_obj_add_event_cb(right, [](lv_event_t *) { forget(&screenLive); }, LV_EVENT_DELETE, nullptr);
}

// ---- C: the desk follows the calendar ----

// The next meeting the desk would stand up for, today.
const Event *nextStandMeeting(time_t now) {
  if (!g_settings.deskMeetings) return nullptr;
  for (const Event &e : g_snap.events) {
    if (e.allDay || e.end / 1000 <= now || day_number(e.start / 1000) != day_number(now)) continue;
    if ((e.end - e.start) / 60000 < kMeetingMinMinutes || e.key == skipped) continue;
    return &e;
  }
  return nullptr;
}

void ruleRow(lv_obj_t *parent, const char *text, bool Settings::*field, int w) {
  const Theme &t = theme();
  lv_obj_t *row = transparent(parent);
  lv_obj_set_width(row, w);
  set_flex(row, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_SPACE_BETWEEN);
  lv_obj_t *l = mk_label(row, &font_14, t.text, text);
  lv_obj_set_width(l, w - 70);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  // The member pointer goes through user data as an index into a small table.
  static bool Settings::*const kFields[] = {&Settings::deskMeetings, &Settings::deskFocus, &Settings::deskAway};
  intptr_t idx = 0;
  while (kFields[idx] != field) idx++;
  mk_switch(row, g_settings.*field, [](lv_event_t *e) {
    Settings s = g_settings;
    bool Settings::*f = kFields[(intptr_t)lv_event_get_user_data(e)];
    s.*f = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    ui_save_settings(s);
    lv_async_call([](void *) { desk_refresh(); }, nullptr);
  }, (void *)idx);
}

void buildScreenC(const DeskStatus &st) {
  const Theme &t = theme();
  const int w = SCREEN_W - 2 * PAD;
  const time_t now = ui_now();
  lv_obj_t *col = transparent(deskBody);
  lv_obj_set_size(col, w, BODY_H);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_add_event_cb(col, [](lv_event_t *) { forget(&screenLive); }, LV_EVENT_DELETE, nullptr);
  lv_obj_t *h = mk_label(col, &font_12, t.muted, kTodayAtDesk);
  lv_obj_set_style_text_letter_space(h, 1, 0);

  // Timeline from kDayFrom to kDayTo: what happened, and the meetings ahead.
  lv_obj_t *line = mk_box(col, t.panel, 14);
  lv_obj_set_size(line, w, 112);
  lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
  const int lx = 14, lw = w - 28, ly = 36, lh = 22;
  const int span = (kDayTo - kDayFrom) * 60;
  for (int hr = kDayFrom; hr <= kDayTo; hr += 2) {
    lv_obj_t *l = mk_label(line, &font_12, t.muted, strf("%02d:00", hr).c_str());
    lv_obj_set_pos(l, lx + (hr - kDayFrom) * 60 * lw / span - (hr == kDayTo ? 36 : 0), 8);
  }
  lv_obj_t *track = mk_box(line, t.raised, 6);
  lv_obj_set_pos(track, lx, ly);
  lv_obj_set_size(track, lw, lh);
  for (int s = kDayFrom * 12; s < kDayTo * 12; s++) {
    if (slots[s] != 2) continue;
    int e = s;
    while (e < kDayTo * 12 && slots[e] == 2) e++;
    lv_obj_t *b = mk_box(line, t.good, 6);
    lv_obj_set_pos(b, lx + (s * 5 - kDayFrom * 60) * lw / span, ly);
    lv_obj_set_size(b, std::max(4, (e - s) * 5 * lw / span), lh);
    s = e;
  }
  if (g_settings.deskMeetings) {
    for (const Event &e : g_snap.events) {
      if (e.allDay || e.end / 1000 <= now || day_number(e.start / 1000) != day_number(now)) continue;
      if ((e.end - e.start) / 60000 < kMeetingMinMinutes) continue;
      struct tm a, b;
      const time_t st0 = std::max<time_t>(e.start / 1000, now), en = e.end / 1000;
      localtime_r(&st0, &a);
      localtime_r(&en, &b);
      const int m0 = std::max(0, a.tm_hour * 60 + a.tm_min - kDayFrom * 60);
      const int m1 = std::min(span, b.tm_hour * 60 + b.tm_min - kDayFrom * 60);
      if (m1 <= m0) continue;
      lv_obj_t *p = mk_box(line, t.accent, 6);
      lv_obj_set_style_bg_opa(p, e.key == skipped ? LV_OPA_20 : LV_OPA_50, 0);
      lv_obj_set_style_border_color(p, c(t.accent), 0);
      lv_obj_set_style_border_width(p, 2, 0);
      lv_obj_set_pos(p, lx + m0 * lw / span, ly);
      lv_obj_set_size(p, std::max(4, (m1 - m0) * lw / span), lh);
    }
  }
  struct tm tm;
  localtime_r(&now, &tm);
  const int nowM = tm.tm_hour * 60 + tm.tm_min - kDayFrom * 60;
  if (nowM > 0 && nowM < span) {
    lv_obj_t *n = mk_box(line, t.text, 1);
    lv_obj_set_pos(n, lx + nowM * lw / span, ly - 6);
    lv_obj_set_size(n, 2, lh + 12);
  }
  lv_obj_t *legend = transparent(line);
  lv_obj_set_pos(legend, lx, ly + lh + 12);
  set_flex(legend, LV_FLEX_FLOW_ROW, 14);
  const struct {
    uint32_t color;
    const char *text;
  } keys[] = {{t.good, kStood}, {t.raised, kSat}, {t.accent, kPlanned}};
  for (const auto &k : keys) {
    lv_obj_t *r = transparent(legend);
    set_flex(r, LV_FLEX_FLOW_ROW, 6);
    mk_dot(r, k.color, 10);
    mk_label(r, &font_12, t.muted, k.text);
  }

  lv_obj_t *bottom = transparent(col);
  lv_obj_set_width(bottom, w);
  lv_obj_set_flex_grow(bottom, 1);
  set_flex(bottom, LV_FLEX_FLOW_ROW, GAP, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  const int cw = (w - GAP) / 2;
  lv_obj_t *next = mk_box(bottom, t.card, 14);
  lv_obj_set_size(next, cw, lv_pct(100));
  lv_obj_set_style_pad_all(next, 16, 0);
  set_flex(next, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  const Event *ev = nextStandMeeting(now);
  if (ev) {
    lv_obj_set_style_border_color(next, c(t.accent), 0);
    lv_obj_set_style_border_width(next, 2, 0);
    const long in = (long)(ev->start / 1000 - now);
    String title = ev->title;
    title.toUpperCase();
    lv_obj_t *hd = mk_label(next, &font_12, t.accent,
                            (in > 0 ? strf(kInMin, (in + 59) / 60, title.c_str()) : strf(kNowIn, title.c_str())).c_str());
    lv_obj_set_width(hd, cw - 32);
    lv_label_set_long_mode(hd, LV_LABEL_LONG_DOT);
    mk_label(next, &font_20b, t.text, kStandingFor);
    lv_obj_t *sub = mk_label(next, &font_12, t.muted,
                             strf(kStandPlan, g_settings.deskStandSlot, fmt_time(ev->start / 1000).c_str(),
                                  g_settings.deskSitSlot).c_str());
    lv_obj_set_width(sub, cw - 32);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
  } else {
    mk_label(next, &font_20b, t.text, kNoPlan);
    mk_label(next, &font_12, t.muted, kNoPlanSub);
  }
  lv_obj_t *fill = transparent(next);
  lv_obj_set_flex_grow(fill, 1);
  lv_obj_t *row = transparent(next);
  lv_obj_set_width(row, cw - 32);
  set_flex(row, LV_FLEX_FLOW_ROW, 10);
  if (ev) {
    lv_obj_t *stay = mk_text_button(row, kStaySeated, false);
    on_click(stay, [](lv_event_t *) {
      const Event *e = nextStandMeeting(ui_now());
      if (e) skipped = e->key;
      if (raisedFor.length()) desk_recall(g_settings.deskSitSlot);
      raisedFor = "";
      lv_async_call([](void *) { desk_refresh(); }, nullptr);
    });
    lv_obj_t *now = mk_text_button(row, kStandNow, true);
    on_click(now, [](lv_event_t *) {
      const Event *e = nextStandMeeting(ui_now());
      if (e) raisedFor = e->key;
      desk_recall(g_settings.deskStandSlot);
    });
  } else {
    memButton(row, g_settings.deskSitSlot, screenLive, (cw - 42) / 2, 48);
    memButton(row, g_settings.deskStandSlot, screenLive, (cw - 42) / 2, 48);
  }

  lv_obj_t *rules = mk_box(bottom, t.card, 14);
  lv_obj_set_size(rules, cw, lv_pct(100));
  lv_obj_set_style_pad_all(rules, 16, 0);
  set_flex(rules, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *rh = mk_label(rules, &font_12, t.muted, kRules);
  lv_obj_set_style_text_letter_space(rh, 1, 0);
  ruleRow(rules, kRuleMeetings, &Settings::deskMeetings, cw - 32);
  ruleRow(rules, kRuleFocus, &Settings::deskFocus, cw - 32);
  ruleRow(rules, kRuleAway, &Settings::deskAway, cw - 32);
  lv_obj_t *f2 = transparent(rules);
  lv_obj_set_flex_grow(f2, 1);
  lv_obj_t *status = transparent(rules);
  set_flex(status, LV_FLEX_FLOW_ROW, 6);
  screenLive.height = mk_label(status, &font_12, t.muted, "");
  mk_label(status, &font_12, t.muted, "\xC2\xB7");
  screenLive.state = mk_label(status, &font_12, t.muted, "");
  screenLive.today = mk_label(rules, &font_12, t.muted, "");
  (void)st;
}

void closePrompt() { close_overlay(&prompt); }

// C: a minute before a meeting, say the desk is about to stand up.
void showPrompt(const Event &ev) {
  closePrompt();
  const Theme &t = theme();
  mk_overlay(true, &prompt);
  lv_obj_t *card = mk_box(prompt, t.card, 22);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(card, 480);
  lv_obj_set_style_pad_all(card, 24, 0);
  lv_obj_set_style_border_color(card, c(t.accent), 0);
  lv_obj_set_style_border_width(card, 2, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);
  String title = ev.title;
  title.toUpperCase();
  lv_obj_t *hd = mk_label(card, &font_12, t.accent, strf(kInMin, 1L, title.c_str()).c_str());
  lv_obj_set_width(hd, 432);
  lv_label_set_long_mode(hd, LV_LABEL_LONG_DOT);
  mk_label(card, &font_26b, t.text, kStandingFor);
  lv_obj_t *sub = mk_label(card, &font_14, t.muted,
                           strf(kStandPlan, g_settings.deskStandSlot, fmt_time(ev.start / 1000).c_str(),
                                g_settings.deskSitSlot).c_str());
  lv_obj_set_width(sub, 432);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
  lv_obj_t *row = transparent(card);
  lv_obj_set_size(row, 432, 56);
  set_flex(row, LV_FLEX_FLOW_ROW, 10);
  lv_obj_t *stay = mk_text_button(row, kStaySeated, false, 211);
  lv_obj_set_height(stay, 52);
  on_click(stay, [](lv_event_t *) {
    skipped = promptedFor;
    lv_async_call([](void *) { closePrompt(); }, nullptr);
  });
  lv_obj_t *now = mk_text_button(row, kStandNow, true, 211);
  lv_obj_set_height(now, 52);
  on_click(now, [](lv_event_t *) {
    raisedFor = promptedFor;
    desk_recall(g_settings.deskStandSlot);
    lv_async_call([](void *) { closePrompt(); }, nullptr);
  });
}

void automate(const DeskStatus &st, time_t now) {
  // Meetings: stand at the start, sit again at the end.
  if (g_settings.deskMeetings) {
    const Event *ev = nextStandMeeting(now);
    if (ev) {
      const long in = (long)(ev->start / 1000 - now);
      if (in > 0 && in <= kPromptBefore && promptedFor != ev->key && !overlay_visible()) {
        promptedFor = ev->key;
        showPrompt(*ev);
      }
      if (in <= 0 && raisedFor != ev->key) {
        closePrompt();
        raisedFor = ev->key;
        desk_recall(g_settings.deskStandSlot);
      }
    }
  }
  if (raisedFor.length()) {
    bool still = false;
    for (const Event &e : g_snap.events) still |= e.key == raisedFor && e.end / 1000 > now;
    if (!still) {
      raisedFor = "";
      desk_recall(g_settings.deskSitSlot);
    }
  }
  // Focus timer
  const bool focus = focus_running();
  if (g_settings.deskFocus && focus && !wasFocus && st.heightMm < kStandingMm) {
    focusRaised = true;
    desk_recall(g_settings.deskStandSlot);
  }
  if (!focus && wasFocus && focusRaised) {
    focusRaised = false;
    desk_recall(g_settings.deskSitSlot);
  }
  wasFocus = focus;
  // Phone presence
  if (g_settings.deskAway && g_settings.presence && st.hasLogin) {
    const int away = presence_away() ? 1 : 0;
    if (away != awaySent) {
      if (awaySent >= 0 || away) desk_away(away);
      awaySent = away;
    }
  }
}

void sample(const DeskStatus &st, time_t now) {
  const long day = day_number(now);
  if (day != slotsDay) {
    slotsDay = day;
    memset(slots, 0, sizeof(slots));
    standMinutesToday = changesToday = 0;
  }
  const String s = st.online ? stateText(st) : String("");
  if (s != lastState) {
    if (!st.moving) stateSince = now;
    lastState = s;
  }
  if (!st.online || st.moving || !clock_valid()) return;
  struct tm tm;
  localtime_r(&now, &tm);
  const int minute = tm.tm_hour * 60 + tm.tm_min;
  if (minute == lastMinuteSampled) return;
  lastMinuteSampled = minute;
  const bool standing = st.heightMm >= kStandingMm;
  slots[minute / 5] = standing ? 2 : 1;
  standMinutesToday += standing;
  if (standing != wasStanding) changesToday++;
  wasStanding = standing;
}

}  // namespace

bool desk_tab() { return g_settings.desk && g_settings.layout(Layout::Desk) != 0; }

// A: the dock along the bottom of Home.
lv_obj_t *mk_desk_dock(lv_obj_t *parent, int w) {
  if (!on(0)) return nullptr;
  const Theme &t = theme();
  const DeskStatus st = desk_status();
  Live &live = dockLive;
  lv_obj_t *dock = mk_box(parent, t.card, 16);
  lv_obj_set_size(dock, w, DOCK_H);
  lv_obj_set_style_pad_left(dock, 16, 0);
  lv_obj_set_style_pad_right(dock, 8, 0);
  set_flex(dock, LV_FLEX_FLOW_ROW, 10);
  lv_obj_clear_flag(dock, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(dock, [](lv_event_t *) { forget(&dockLive); }, LV_EVENT_DELETE, nullptr);
  lv_obj_t *hc = transparent(dock);
  lv_obj_set_width(hc, 110);
  set_flex(hc, LV_FLEX_FLOW_COLUMN, 0, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  live.height = mk_label(hc, &font_20b, t.text, "");
  live.state = mk_label(hc, &font_12, t.muted, "");
  lv_label_set_long_mode(live.state, LV_LABEL_LONG_DOT);
  lv_obj_set_width(live.state, 110);
  // Timer: tap to start or stop it (needs the desk login).
  lv_obj_t *tc = mk_button(dock, t.card, 8);
  lv_obj_set_size(tc, 124, 48);
  set_flex(tc, LV_FLEX_FLOW_COLUMN, 4, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  live.timer = mk_label(tc, &font_12, t.accent, "");
  live.timerBar = lv_bar_create(tc);
  lv_obj_set_size(live.timerBar, 116, 5);
  lv_bar_set_range(live.timerBar, 0, 1000);
  lv_obj_set_style_bg_color(live.timerBar, c(t.line), LV_PART_MAIN);
  lv_obj_set_style_bg_color(live.timerBar, c(t.accent), LV_PART_INDICATOR);
  if (st.hasLogin) on_click(tc, [](lv_event_t *) { desk_timer(!desk_status().timerRunning); });
  const int buttons = st.hasLogin ? 2 : 0;
  const int memW = (w - 16 - 8 - 110 - 124 - buttons * 58 - 10 * (3 + buttons) - 3 * 6) / 4;
  lv_obj_t *mems = transparent(dock);
  set_flex(mems, LV_FLEX_FLOW_ROW, 6);
  for (uint8_t i = 1; i <= 4; i++) memButton(mems, i, live, memW, 52);
  if (st.hasLogin) {
    holdButton(roundButton(dock, ICON_CARET_UP, 52), 1);
    holdButton(roundButton(dock, ICON_CARET_DN, 52), -1);
  }
  // Stop sits on top of the memory buttons while the desk moves.
  live.stop = mk_button(dock, t.badBg, 14);
  lv_obj_add_flag(live.stop, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(live.stop, 4 * memW + 18, 52);
  lv_obj_align(live.stop, LV_ALIGN_LEFT_MID, 110 + 124 + 20, 0);
  lv_obj_center(mk_label(live.stop, &font_14b, t.badFg, kStop));
  on_click(live.stop, [](lv_event_t *) { desk_stop(); });
  updateLive(live, st);
  return dock;
}

lv_obj_t *desk_create() {
  lv_obj_t *scr = mk_screen();
  mk_nav_header(scr, Screen::Desk);
  deskBody = mk_box(scr, 0, 0);
  lv_obj_set_style_bg_opa(deskBody, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(deskBody, PAD, BODY_Y);
  lv_obj_set_size(deskBody, SCREEN_W - 2 * PAD, BODY_H);
  set_flex(deskBody, LV_FLEX_FLOW_ROW, GAP, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(deskBody, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

void desk_refresh() {
  if (!deskBody) return;
  lv_obj_clean(deskBody);
  const DeskStatus st = desk_status();
  if (g_settings.layout(Layout::Desk) == 2) buildScreenC(st);
  else buildScreenB(st);
  updateLive(screenLive, st);
}

void desk_tick() {
  if (!g_settings.desk) return;
  if (millis() - lastTick < 500) return;
  lastTick = millis();
  const DeskStatus st = desk_status();
  const time_t now = ui_now();
  sample(st, now);
  if (on(2) && st.online) automate(st, now);
  // Buttons that need the login appear once one is set.
  static bool hadLogin = false;
  if (st.hasLogin != hadLogin) {
    hadLogin = st.hasLogin;
    ui_refresh_all();
    return;
  }
  updateLive(dockLive, st);
  updateLive(screenLive, st);
  seenVersion = st.version;
}

String desk_status_text() {
  const DeskStatus st = desk_status();
  if (!g_settings.desk) return "";
  if (!st.online) return st.error.length() ? st.error : String(kOffline);
  String s = cm(st.heightMm, true) + " \xC2\xB7 " + stateText(st);
  if (st.hasLogin && !st.loggedIn && st.error.length()) s += " \xC2\xB7 " + st.error;
  return s;
}
