// Meeting timer and meeting cost (mockup rows "mt" and "cm").
// Timer: A progress on the Home card, B a full-screen countdown, C a glowing
// top edge with a pill. All three chime 5 minutes before the end.
// Cost: A a line on the Home card, B a big counter on the full-screen view,
// C a weekly receipt opened from the Week screen.
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kMinLeft = "%d MIN LEFT";
constexpr const char *kSecLeft = "%d SEC LEFT";
constexpr const char *kChimeAt = "Soft chime at %s";
constexpr const char *kEndsAt = "Ends at %s";
constexpr const char *kPillLeft = "%d min left";
constexpr const char *kInMeeting = "IN THIS MEETING";
constexpr const char *kCostSoFar = "%s so far \xC2\xB7 %s/h each";
constexpr const char *kCostIfRuns = "%s if it runs to %s";
constexpr const char *kCostPeople = "%d people \xC2\xB7 %s per person per hour";
constexpr const char *kCostLabel = "THIS MEETING HAS COST";
constexpr const char *kHide = "Hide";
constexpr const char *kChimeOn = "Chime on";
constexpr const char *kChimeOff = "Chime off";
constexpr const char *kReceiptTitle = "Meetings this week";
constexpr const char *kReceiptLine = "%d people \xC2\xB7 %s";
constexpr const char *kReceiptTotal = "TOTAL";
constexpr const char *kReceiptHours = "%d meetings \xC2\xB7 %s in meetings";
constexpr const char *kReceiptEmpty = "No meetings with other people this week.";
constexpr const char *kReceiptNote = "People x hours x the rate in Settings > Features > Meeting cost.";
constexpr const char *kHoursMin = "%d h %02d min";
constexpr const char *kMinutesOnly = "%d min";
constexpr const char *kClose = "Close";
constexpr const char *kCurrency = "\xE2\x82\xAC";  // euro sign
constexpr int kChimeBeforeEnd = 5 * 60;  // seconds
constexpr int EDGE_H = 4;                // C: glowing line across the top

// ---- State -------------------------------------------------------------------

String chimed;        // event key that already had its end chime
String fullShownFor;  // event key the full-screen view opened for (once per meeting)
bool chimeMuted = false;  // "Chime off" on the full-screen view, for that meeting
uint32_t lastTick = 0;

lv_obj_t *full = nullptr;  // B: full-screen countdown and counter
lv_obj_t *fullTime, *fullLeft, *fullCost, *fullBar, *fullChime;
lv_obj_t *edge = nullptr, *edgeBar, *edgePill;  // C, on the system layer above everything
lv_obj_t *receipt = nullptr;

// ---- Code --------------------------------------------------------------------

String money(long amount) {
  // Thousands separated by commas: 1,230
  String digits = String(amount);
  String out;
  for (int i = 0; i < (int)digits.length(); i++) {
    if (i && (digits.length() - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return String(kCurrency) + out;
}

bool timerOn(int layout) { return g_settings.meetTimer && g_settings.layout(Layout::MeetTimer) == layout; }
bool costOn(int layout) { return g_settings.costMeter && g_settings.layout(Layout::Cost) == layout; }

int people(const Event &ev) { return ev.guests > 1 ? ev.guests : 1; }

String duration(long secs) {
  const int m = (int)(secs / 60);
  return m >= 60 ? strf(kHoursMin, m / 60, m % 60) : strf(kMinutesOnly, m);
}

void closeFull() {
  close_overlay(&full);
  fullTime = fullLeft = fullCost = fullBar = fullChime = nullptr;
}

void updateChimeButton() {
  if (!fullChime) return;
  const Theme &t = theme();
  lv_label_set_text(lv_obj_get_child(fullChime, 0), chimeMuted ? kChimeOff : kChimeOn);
  lv_obj_set_style_bg_color(fullChime, c(chimeMuted ? t.raised : t.accent), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(fullChime, 0), c(chimeMuted ? t.text : t.onAccent), 0);
}

// B: the meeting fills the screen. Shows the countdown, the cost, or both.
void showFull(const Event &ev) {
  closeFull();
  const Theme &t = theme();
  mk_overlay(false, &full);
  lv_obj_set_style_bg_color(full, c(t.bg), 0);
  lv_obj_set_style_bg_opa(full, LV_OPA_COVER, 0);
  lv_obj_add_flag(full, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *head = mk_label(full, &font_14b, t.accent, kInMeeting);
  lv_obj_set_style_text_letter_space(head, 2, 0);
  lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 26);
  lv_obj_t *title = mk_label(full, &font_26b, t.text, ev.title.c_str());
  lv_obj_set_width(title, SCREEN_W - 120);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);

  const bool timer = timerOn(1);
  int y = 110;
  if (timer) {
    fullTime = mk_label(full, &font_150, t.text, "");
    lv_obj_align(fullTime, LV_ALIGN_TOP_MID, 0, y - 10);
    y += 160;
    fullBar = lv_bar_create(full);
    lv_obj_set_size(fullBar, SCREEN_W - 200, 10);
    lv_obj_align(fullBar, LV_ALIGN_TOP_MID, 0, y);
    lv_bar_set_range(fullBar, 0, 1000);
    lv_obj_set_style_bg_color(fullBar, c(t.raised), LV_PART_MAIN);
    lv_obj_set_style_bg_color(fullBar, c(t.accent), LV_PART_INDICATOR);
    fullLeft = mk_label(full, &font_14, t.muted, "");
    lv_obj_align(fullLeft, LV_ALIGN_TOP_MID, 0, y + 20);
    y += 50;
  }
  if (costOn(1)) {
    if (!timer) {
      lv_obj_t *l = mk_label(full, &font_14b, t.muted, kCostLabel);
      lv_obj_set_style_text_letter_space(l, 2, 0);
      lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y + 10);
      y += 40;
    }
    fullCost = mk_label(full, timer ? &font_34b : &font_72, timer ? t.warnFg : t.text, "");
    lv_obj_align(fullCost, LV_ALIGN_TOP_MID, 0, y);
    y += timer ? 44 : 90;
    lv_obj_t *rate = mk_label(full, &font_14, t.muted,
                              strf(kCostPeople, people(ev), money(g_settings.costRate).c_str()).c_str());
    lv_obj_align(rate, LV_ALIGN_TOP_MID, 0, y);
  }

  lv_obj_t *buttons = mk_box(full, 0, 0);
  lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
  lv_obj_set_size(buttons, SCREEN_W - 80, 56);
  lv_obj_align(buttons, LV_ALIGN_BOTTOM_MID, 0, -20);
  set_flex(buttons, LV_FLEX_FLOW_ROW, 12, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *hide = mk_text_button(buttons, kHide, false, 200);
  lv_obj_set_height(hide, 56);
  on_click(hide, [](lv_event_t *) { lv_async_call([](void *) { closeFull(); }, nullptr); });
  if (timer && g_settings.sound) {
    fullChime = mk_text_button(buttons, kChimeOn, true, 200);
    lv_obj_set_height(fullChime, 56);
    on_click(fullChime, [](lv_event_t *) {
      chimeMuted = !chimeMuted;
      updateChimeButton();
    });
    updateChimeButton();
  }
}

void updateFull(const Event &ev, time_t now) {
  const long left = std::max<long>(0, ev.end / 1000 - now);
  const long total = std::max<long>(1, (ev.end - ev.start) / 1000);
  if (fullTime) {
    lv_label_set_text(fullTime, (left >= 3600 ? strf("%ld:%02ld:%02ld", left / 3600, left / 60 % 60, left % 60)
                                              : strf("%ld:%02ld", left / 60, left % 60)).c_str());
  }
  if (fullBar) lv_bar_set_value(fullBar, (int32_t)(1000 - left * 1000 / total), LV_ANIM_OFF);
  if (fullLeft) lv_label_set_text(fullLeft, strf(kEndsAt, fmt_time(ev.end / 1000).c_str()).c_str());
  if (fullCost) lv_label_set_text(fullCost, money(meeting_cost(ev, now)).c_str());
}

// C: a thin accent line across the top of the screen that fills up as the
// meeting runs, with a small pill under it. Not clickable, never in the way.
void showEdge() {
  if (edge) return;
  const Theme &t = theme();
  edge = mk_box(lv_layer_sys(), 0, 0);
  lv_obj_set_style_bg_opa(edge, LV_OPA_TRANSP, 0);
  lv_obj_set_size(edge, SCREEN_W, 40);
  lv_obj_clear_flag(edge, LV_OBJ_FLAG_CLICKABLE);
  edgeBar = mk_box(edge, t.accent, 0);
  lv_obj_set_size(edgeBar, 0, EDGE_H);
  lv_obj_set_style_shadow_color(edgeBar, c(t.accent), 0);
  lv_obj_set_style_shadow_width(edgeBar, 14, 0);
  lv_obj_set_style_shadow_spread(edgeBar, 1, 0);
  lv_obj_clear_flag(edgeBar, LV_OBJ_FLAG_CLICKABLE);
  edgePill = mk_box(edge, t.accent, 10);
  lv_obj_set_style_pad_hor(edgePill, 10, 0);
  lv_obj_set_style_pad_ver(edgePill, 2, 0);
  lv_obj_align(edgePill, LV_ALIGN_TOP_MID, 0, EDGE_H);
  lv_obj_clear_flag(edgePill, LV_OBJ_FLAG_CLICKABLE);
  mk_label(edgePill, &font_12, t.onAccent, "");
}

void hideEdge() {
  if (edge) lv_obj_del(edge);
  edge = nullptr;
}

void updateEdge(const Event &ev, time_t now) {
  const long left = std::max<long>(0, ev.end / 1000 - now);
  const long total = std::max<long>(1, (ev.end - ev.start) / 1000);
  lv_obj_set_width(edgeBar, (lv_coord_t)(SCREEN_W - left * SCREEN_W / total));
  const bool late = left <= kChimeBeforeEnd;
  const Theme &t = theme();
  lv_obj_set_style_bg_color(edgeBar, c(late ? t.warnFg : t.accent), 0);
  lv_obj_set_style_shadow_color(edgeBar, c(late ? t.warnFg : t.accent), 0);
  lv_obj_set_style_bg_color(edgePill, c(late ? t.warnFg : t.accent), 0);
  lv_label_set_text(lv_obj_get_child(edgePill, 0), strf(kPillLeft, (int)((left + 59) / 60)).c_str());
}

lv_obj_t *line(lv_obj_t *parent, int w, const lv_font_t *font, uint32_t color, const String &left,
               const String &right) {
  lv_obj_t *row = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_width(row, w);
  set_flex(row, LV_FLEX_FLOW_ROW, 8, LV_FLEX_ALIGN_SPACE_BETWEEN);
  lv_obj_t *l = mk_label(row, font, color, left.c_str());
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, w - 110);
  mk_label(row, font, color, right.c_str());
  return row;
}

}  // namespace

const Event *meeting_now() {
  const time_t now = ui_now();
  for (const Event &ev : g_snap.events) {
    if (!ev.allDay && ev.start / 1000 <= now && ev.end / 1000 > now) return &ev;
  }
  return nullptr;
}

long meeting_cost(const Event &ev, time_t upTo) {
  const time_t start = ev.start / 1000;
  const time_t until = std::min<time_t>(upTo, ev.end / 1000);
  if (until <= start) return 0;
  return (long)((int64_t)people(ev) * g_settings.costRate * (until - start) / 3600);
}

// A: progress and cost lines under the time of the meeting you are in.
void mk_meeting_extras(lv_obj_t *card, const Event &ev, int w) {
  const Theme &t = theme();
  const time_t now = ui_now();
  if (timerOn(0)) {
    const long left = std::max<long>(0, ev.end / 1000 - now);
    const long total = std::max<long>(1, (ev.end - ev.start) / 1000);
    const bool late = left <= kChimeBeforeEnd;
    lv_obj_t *bar = lv_bar_create(card);
    lv_obj_set_size(bar, w, 6);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, (int32_t)(1000 - left * 1000 / total), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, c(t.line), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, c(late ? t.warnFg : t.accent), LV_PART_INDICATOR);
    lv_obj_t *l = mk_label(card, &font_12, late ? t.warnFg : t.accent,
                           left >= 60 ? strf(kMinLeft, (int)(left / 60)).c_str() : strf(kSecLeft, (int)left).c_str());
    lv_obj_set_style_text_letter_space(l, 1, 0);
    if (g_settings.sound && left > kChimeBeforeEnd) {
      mk_label(card, &font_12, t.muted, strf(kChimeAt, fmt_time(ev.end / 1000 - kChimeBeforeEnd).c_str()).c_str());
    }
  }
  if (costOn(0)) {
    lv_obj_t *l = mk_label(card, &font_14b, t.warnFg,
                           strf(kCostSoFar, money(meeting_cost(ev, now)).c_str(), money(g_settings.costRate).c_str()).c_str());
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_t *m = mk_label(card, &font_12, t.muted,
                           strf(kCostIfRuns, money(meeting_cost(ev, ev.end / 1000)).c_str(),
                                fmt_time(ev.end / 1000).c_str()).c_str());
    lv_obj_set_width(m, w);
    lv_label_set_long_mode(m, LV_LABEL_LONG_DOT);
  }
}

bool meeting_receipt_on() { return costOn(2); }

// Cost C: every meeting of the week on the Week screen, priced like a receipt.
void meeting_receipt_show() {
  close_overlay(&receipt);
  const Theme &t = theme();
  mk_overlay(true, &receipt);
  lv_obj_t *card = mk_box(receipt, t.card, 20);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, 520, SCREEN_H - 40);
  lv_obj_set_style_pad_all(card, 22, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);
  const int w = 476;
  mk_label(card, &font_20b, t.text, kReceiptTitle);
  const time_t weekStart = g_snap.weekStart ? g_snap.weekStart : day_start(ui_now());
  mk_label(card, &font_12, t.muted,
           (fmt_date_short(weekStart) + " - " + fmt_date_short(weekStart + 6 * 86400)).c_str());

  lv_obj_t *list = mk_scroll_column(card, 6);
  lv_obj_set_width(list, w);
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_style_border_side(list, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(list, c(t.line), 0);
  lv_obj_set_style_border_width(list, 1, 0);
  lv_obj_set_style_pad_ver(list, 8, 0);
  const std::vector<Event> &events = g_snap.week.empty() ? g_snap.events : g_snap.week;
  long sum = 0, secs = 0;
  int n = 0;
  for (const Event &ev : events) {
    if (ev.allDay || ev.guests < 2) continue;
    const long cost = meeting_cost(ev, ev.end / 1000);
    sum += cost;
    secs += (long)((ev.end - ev.start) / 1000);
    n++;
    line(list, w - 4, &font_14b, t.text, ev.title, money(cost));
    lv_obj_t *sub = mk_label(list, &font_12, t.muted,
                             (fmt_date_short(ev.start / 1000) + "  " +
                              strf(kReceiptLine, people(ev), duration((long)((ev.end - ev.start) / 1000)).c_str()))
                                 .c_str());
    lv_obj_set_style_pad_bottom(sub, 4, 0);
  }
  if (!n) mk_label(list, &font_14, t.muted, kReceiptEmpty);
  line(card, w, &font_20b, t.warnFg, kReceiptTotal, money(sum));
  mk_label(card, &font_12, t.muted, strf(kReceiptHours, n, duration(secs).c_str()).c_str());
  lv_obj_t *note = mk_label(card, &font_12, t.muted, kReceiptNote);
  lv_obj_set_width(note, w);
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
  lv_obj_t *close = mk_text_button(card, kClose, true, w);
  on_click(close, [](lv_event_t *) { lv_async_call([](void *) { close_overlay(&receipt); }, nullptr); });
}

void meeting_tick() {
  if (millis() - lastTick < 1000) return;
  lastTick = millis();
  const Event *ev = (g_settings.meetTimer || g_settings.costMeter) ? meeting_now() : nullptr;
  const time_t now = ui_now();
  const bool night = ui_current() == Screen::Night;

  // End chime, every layout.
  if (ev && g_settings.meetTimer && !chimeMuted && chimed != ev->key && ev->end / 1000 - now <= kChimeBeforeEnd &&
      ev->end - ev->start > 2 * kChimeBeforeEnd * 1000LL) {
    chimed = ev->key;
    if (!night) play(Sound::Chime);
  }

  // B: opens once when a meeting starts, closes when it ends.
  const bool wantsFull = timerOn(1) || costOn(1);
  if (ev && wantsFull && !night) {
    if (fullShownFor != ev->key && !overlay_visible()) {
      fullShownFor = ev->key;
      chimeMuted = false;
      showFull(*ev);
    }
    if (full) updateFull(*ev, now);
  } else if (full) {
    closeFull();
  }

  if (ev && timerOn(2) && !night) {
    showEdge();
    updateEdge(*ev, now);
  } else {
    hideEdge();
  }
}

// Theme or layout changes: the views are rebuilt next tick in the new style.
void meeting_reset() {
  fullShownFor = "";  // settings changed: show the full-screen view again in the new style
  closeFull();
  close_overlay(&receipt);
  hideEdge();
}
