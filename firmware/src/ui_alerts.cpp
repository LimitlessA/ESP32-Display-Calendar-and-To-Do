// Meeting conflicts (mockup row 4: A banner on Home, B a compare popup, C marks
// in the week view) and leave-by alerts (row 5: A full-screen alert, B a card
// on Home, C a countdown in the header).
#include "sync.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kOverlapTitle = "%d meetings overlap";
constexpr const char *kOverlapOne = "%s and %s overlap %s - %s";  // titles, times
constexpr const char *kOverlapMore = "%s and %s overlap, and %d more";
constexpr const char *kCompare = "Compare";
constexpr const char *kConflict = "MEETINGS OVERLAP";
constexpr const char *kOverlapFor = "Overlap %s - %s (%d min)";
constexpr const char *kHideOne = "Hide this one";
constexpr const char *kKeepBoth = "Keep both";
constexpr const char *kHidden = "Hidden from the display. Your calendar is unchanged.";

constexpr const char *kLeaveNow = "LEAVE NOW";
constexpr const char *kLeaveIn = "LEAVE IN %ld MIN";
constexpr const char *kLeaveCard = "Leave in %ld min";
constexpr const char *kLeaveCardNow = "Leave now";
constexpr const char *kTravel = "%d min travel from home";
constexpr const char *kTravelLeave = "%d min travel from home. Leave by %s.";
constexpr const char *kStartsAt = "Starts at %s";
constexpr const char *kGotIt = "Got it";
constexpr const char *kDetails = "Details";

constexpr int kConflictHours = 24;   // B: pop up for overlaps starting within this time
constexpr uint32_t kTickMs = 1000;

// ---- State -------------------------------------------------------------------

lv_obj_t *popup = nullptr;
Conflict shownConflict;
std::vector<String> poppedConflicts;  // B: pairs already shown
std::vector<String> alertedLeave;     // event keys already alerted
lv_obj_t *leaveLayer = nullptr;
Event leaveEvent;
lv_obj_t *leaveCardLabel = nullptr;   // B: countdown on the Home card
String leaveShownKey;                 // event the Home card was built for
uint32_t lastTick = 0;

// ---- Code --------------------------------------------------------------------

String pairKey(const Conflict &cf) { return cf.a.key + "|" + cf.b.key; }

bool seen(std::vector<String> &list, const String &key) {
  for (const String &k : list) {
    if (k == key) return true;
  }
  if (list.size() > 40) list.erase(list.begin());
  list.push_back(key);
  return false;
}

lv_obj_t *transparent(lv_obj_t *parent) {
  lv_obj_t *o = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  return o;
}

std::vector<Conflict> upcoming() {
  const time_t now = ui_now();
  return find_conflicts(g_snap.events, now, now + 7 * 86400);
}

// One side of the compare popup.
void eventColumn(lv_obj_t *parent, const Event &ev, int w) {
  const Theme &t = theme();
  lv_obj_t *col = mk_box(parent, t.card, 16);
  lv_obj_set_size(col, w, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(col, 16, 0);
  lv_obj_set_style_border_side(col, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_width(col, 4, 0);
  lv_obj_set_style_border_color(col, c(ev.color), 0);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 6, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  mk_label(col, &font_20b, t.text, (fmt_time(ev.start / 1000) + " - " + fmt_time(ev.end / 1000)).c_str());
  lv_obj_t *title = mk_label(col, &font_16s, t.text, event_title(ev).c_str());
  lv_obj_set_width(title, w - 32);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  lv_obj_t *cal = mk_label(col, &font_12, t.muted, ev.calendar.c_str());
  lv_obj_set_width(cal, w - 32);
  lv_label_set_long_mode(cal, LV_LABEL_LONG_DOT);
  lv_obj_t *hide = mk_text_button(col, kHideOne, false, w - 32);
  lv_obj_set_style_pad_top(hide, 0, 0);
  Event *copy = attach(hide, ev);
  on_click(hide, [](lv_event_t *e) {
    lv_async_call([](void *p) {
      Event *ev = static_cast<Event *>(p);
      close_overlay(&popup);
      sync_dismiss_event(*ev);
      toast(kHidden);
      delete ev;
    }, new Event(*static_cast<Event *>(lv_event_get_user_data(e))));
  }, copy);
}

void leaveHide() { close_overlay(&leaveLayer); }

// A: full-screen alert when it is time to get ready.
void leaveAlertShow(const Event &ev, time_t leaveAt) {
  const Theme &t = theme();
  leaveHide();
  leaveEvent = ev;
  mk_overlay(false, &leaveLayer);
  lv_obj_t *icon = mk_box(leaveLayer, t.warnBg, 40);
  lv_obj_set_size(icon, 80, 80);
  lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_center(mk_label(icon, &font_20b, t.accent, ICON_WALK));
  const long mins = (long)((leaveAt - ui_now() + 59) / 60);
  lv_obj_t *head = mk_label(leaveLayer, &font_34b, t.accent, mins > 0 ? strf(kLeaveIn, mins).c_str() : kLeaveNow);
  lv_obj_set_style_text_letter_space(head, 1, 0);
  lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 140);
  lv_obj_t *title = mk_label(leaveLayer, &font_26b, t.text, event_title(ev).c_str());
  lv_obj_set_width(title, SCREEN_W - 120);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 196);
  String info = strf(kStartsAt, fmt_time(ev.start / 1000).c_str()) + "    " + ev.location;
  lv_obj_t *where = mk_label(leaveLayer, &font_16, t.muted, info.c_str());
  lv_obj_set_width(where, SCREEN_W - 120);
  lv_obj_set_style_text_align(where, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(where, LV_LABEL_LONG_DOT);
  lv_obj_align(where, LV_ALIGN_TOP_MID, 0, 240);
  lv_obj_t *travel = mk_label(leaveLayer, &font_16s, t.text, travel_text(ev).c_str());
  lv_obj_align(travel, LV_ALIGN_TOP_MID, 0, 272);

  lv_obj_t *row = transparent(leaveLayer);
  lv_obj_set_size(row, 460, 64);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -40);
  set_flex(row, LV_FLEX_FLOW_ROW, 12, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *details = mk_text_button(row, kDetails, false, 200);
  lv_obj_set_height(details, 60);
  on_click(details, [](lv_event_t *) {
    leaveHide();
    ui_open_event(leaveEvent);
  });
  lv_obj_t *ok = mk_text_button(row, kGotIt, true, 240);
  lv_obj_set_height(ok, 60);
  on_click(ok, [](lv_event_t *) { leaveHide(); });
}

String leaveCardText(time_t leaveAt) {
  const long mins = (long)((leaveAt - ui_now() + 59) / 60);
  return mins > 0 ? strf(kLeaveCard, mins) : String(kLeaveCardNow);
}

}  // namespace

std::vector<Conflict> find_conflicts(const std::vector<Event> &events, time_t from, time_t to) {
  std::vector<Conflict> out;
  for (size_t i = 0; i < events.size(); i++) {
    const Event &a = events[i];
    if (a.allDay || a.end / 1000 <= from || a.start / 1000 >= to) continue;
    for (size_t j = i + 1; j < events.size(); j++) {
      const Event &b = events[j];
      if (b.allDay || b.end / 1000 <= from) continue;
      if (b.start >= a.end) continue;  // sorted by start: b starts after a ends
      if (b.end <= a.start) continue;
      const time_t s = (a.start > b.start ? a.start : b.start) / 1000;
      const time_t e = (a.end < b.end ? a.end : b.end) / 1000;
      if (e > s) out.push_back({a, b, s, e});
    }
  }
  return out;
}

bool has_conflict(const Event &ev, const std::vector<Conflict> &all, String *other) {
  for (const Conflict &cf : all) {
    if (cf.a.key == ev.key || cf.b.key == ev.key) {
      if (other) *other = cf.a.key == ev.key ? cf.b.title : cf.a.title;
      return true;
    }
  }
  return false;
}

void conflict_popup_show(const Conflict &cf) {
  const Theme &t = theme();
  close_overlay(&popup);
  shownConflict = cf;
  mk_overlay(true, &popup);
  lv_obj_t *card = mk_box(popup, t.panel, 24);
  lv_obj_set_width(card, 700);
  lv_obj_set_style_pad_all(card, 24, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 14, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);

  lv_obj_t *head = transparent(card);
  set_flex(head, LV_FLEX_FLOW_ROW, 10);
  mk_label(head, &font_16, t.badFg, ICON_WARNING);
  lv_obj_t *h = mk_label(head, &font_14b, t.badFg, kConflict);
  lv_obj_set_style_text_letter_space(h, 1, 0);
  const int mins = (int)((cf.to - cf.from) / 60);
  mk_label(card, &font_16, t.muted, strf(kOverlapFor, fmt_time(cf.from).c_str(), fmt_time(cf.to).c_str(), mins).c_str());

  lv_obj_t *cols = transparent(card);
  lv_obj_set_width(cols, 652);
  set_flex(cols, LV_FLEX_FLOW_ROW, 12, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  eventColumn(cols, cf.a, 320);
  eventColumn(cols, cf.b, 320);

  lv_obj_t *keep = mk_text_button(card, kKeepBoth, true, 652);
  lv_obj_set_height(keep, 52);
  on_click(keep, [](lv_event_t *) { close_overlay(&popup); });
}

lv_obj_t *mk_conflict_banner(lv_obj_t *parent, int w) {
  if (!g_settings.conflicts || g_settings.layout(Layout::Conflicts) != 0) return nullptr;
  const std::vector<Conflict> all = upcoming();
  if (all.empty()) return nullptr;
  const Theme &t = theme();
  const Conflict &cf = all[0];
  lv_obj_t *b = mk_button(parent, t.badBg, 12);
  lv_obj_set_size(b, w, 44);
  lv_obj_set_style_pad_hor(b, 14, 0);
  set_flex(b, LV_FLEX_FLOW_ROW, 10);
  mk_label(b, &font_16, t.badFg, ICON_WARNING);
  String text = all.size() == 1
                    ? strf(kOverlapOne, cf.a.title.c_str(), cf.b.title.c_str(), fmt_time(cf.from).c_str(),
                           fmt_time(cf.to).c_str())
                    : strf(kOverlapMore, cf.a.title.c_str(), cf.b.title.c_str(), (int)all.size() - 1);
  if (day_number(cf.from) != day_number(ui_now())) text = fmt_date_short(cf.from) + ": " + text;
  lv_obj_t *l = mk_label(b, &font_14b, t.badFg, text.c_str());
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(l, 1);
  lv_obj_t *cmp = mk_label(b, &font_14b, t.text, kCompare);
  lv_obj_set_style_pad_left(cmp, 6, 0);
  Conflict *copy = attach(b, cf);
  on_click(b, [](lv_event_t *e) { conflict_popup_show(*static_cast<Conflict *>(lv_event_get_user_data(e))); }, copy);
  return b;
}

String travel_text(const Event &ev) {
  if (!ev.travelMin) return "";
  const time_t leaveAt = ev.start / 1000 - ev.travelMin * 60;
  return strf(kTravelLeave, (int)ev.travelMin, fmt_time(leaveAt).c_str());
}

const Event *leave_next(time_t &leaveAt) {
  if (!g_settings.leaveBy) return nullptr;
  const time_t now = ui_now();
  for (const Event &ev : g_snap.events) {
    const time_t start = ev.start / 1000;
    if (ev.allDay || !ev.travelMin || start <= now) continue;
    const time_t at = start - ev.travelMin * 60;
    if (now < at - g_settings.leaveLead * 60) continue;
    leaveAt = at;
    return &ev;
  }
  return nullptr;
}

lv_obj_t *mk_leave_card(lv_obj_t *parent, int w) {
  leaveCardLabel = nullptr;
  leaveShownKey = "";
  if (g_settings.layout(Layout::LeaveBy) != 1) return nullptr;
  time_t leaveAt;
  const Event *ev = leave_next(leaveAt);
  if (!ev) return nullptr;
  const Theme &t = theme();
  leaveShownKey = ev->key;
  lv_obj_t *card = mk_button(parent, t.warnBg, 12);
  lv_obj_set_width(card, w);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_style_border_color(card, c(t.accent), 0);
  lv_obj_set_style_border_width(card, 2, 0);
  set_flex(card, LV_FLEX_FLOW_ROW, 12);
  lv_obj_t *icon = mk_label(card, &font_20b, t.accent, ICON_WALK);
  lv_obj_set_width(icon, 30);
  lv_obj_t *col = transparent(card);
  lv_obj_set_flex_grow(col, 1);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  leaveCardLabel = mk_label(col, w < 240 ? &font_16s : &font_20b, t.accent, leaveCardText(leaveAt).c_str());
  lv_obj_add_event_cb(card, [](lv_event_t *) { leaveCardLabel = nullptr; }, LV_EVENT_DELETE, nullptr);
  lv_obj_t *title = mk_label(col, &font_14b, t.text, (event_title(*ev) + " \xC2\xB7 " + fmt_time(ev->start / 1000)).c_str());
  lv_obj_set_width(title, w - 24 - 42);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  mk_label(col, &font_12, t.muted, strf(kTravel, (int)ev->travelMin).c_str());
  Event *copy = attach(card, *ev);
  on_click(card, [](lv_event_t *e) { ui_open_event(*static_cast<Event *>(lv_event_get_user_data(e))); }, copy);
  return card;
}

void alerts_tick() {
  if (millis() - lastTick < kTickMs) return;
  lastTick = millis();

  // Leave-by: a sound (and in layout A a full-screen alert) once per meeting.
  time_t leaveAt = 0;
  const Event *ev = leave_next(leaveAt);
  const uint8_t leaveLayout = g_settings.layout(Layout::LeaveBy);
  if (ev && !seen(alertedLeave, ev->key)) {
    play(Sound::Alert);
    lv_disp_trig_activity(nullptr);
    if (leaveLayout == 0 && ui_current() != Screen::Night) leaveAlertShow(*ev, leaveAt);
  }
  if (leaveLayout == 1) {
    const String key = ev ? ev->key : String("");
    if (key != leaveShownKey && ui_current() == Screen::Home) home_refresh();
    else if (ev && leaveCardLabel) lv_label_set_text(leaveCardLabel, leaveCardText(leaveAt).c_str());
  }

  // Conflicts, layout B: compare popup once for each new overlap coming up soon.
  if (g_settings.conflicts && g_settings.layout(Layout::Conflicts) == 1 && !overlay_visible() &&
      (ui_current() == Screen::Home || ui_current() == Screen::Week || ui_current() == Screen::Tasks)) {
    const time_t now = ui_now();
    for (const Conflict &cf : find_conflicts(g_snap.events, now, now + kConflictHours * 3600)) {
      if (!seen(poppedConflicts, pairKey(cf))) {
        conflict_popup_show(cf);
        break;
      }
    }
  }
}
