// Phone presence (mockup row "pp"): the display notices when your phone leaves.
// A: a "welcome back" summary of what changed while you were away,
// B: privacy mode, meeting titles read "Busy" while you are away,
// C: an away screen, the dim night clock until you are back.
#include <map>
#include <set>

#include "presence.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kBusy = "Busy";
constexpr const char *kWelcome = "Welcome back";
constexpr const char *kAwayFor = "You were away for %s";
constexpr const char *kNewMeetings = "%d new meetings";
constexpr const char *kNewMeeting = "1 new meeting";
constexpr const char *kMoved = "%d meetings moved";
constexpr const char *kMovedOne = "1 meeting moved";
constexpr const char *kTasksGone = "%d tasks done or removed elsewhere";
constexpr const char *kTaskGone = "1 task done or removed elsewhere";
constexpr const char *kNewTasks = "%d new tasks";
constexpr const char *kNewTask = "1 new task";
constexpr const char *kNothingNew = "Nothing changed while you were away";
constexpr const char *kNext = "NEXT";
constexpr const char *kNoNext = "No more meetings today";
constexpr const char *kShowDay = "Show my day";
constexpr const char *kClose = "Close";
constexpr const char *kHoursMin = "%d h %d min";
constexpr const char *kMinutes = "%d min";
constexpr const char *kPairTitle = "Pair your phone";
constexpr const char *kPairSteps =
    "1. Open Bluetooth settings on your phone.\n"
    "2. Pick \"Calendar display\" from the list of devices.\n"
    "3. Accept the pairing request.\n"
    "Your phone reconnects by itself from now on. Nothing is sent to it.";
constexpr const char *kPairWaiting = "Waiting for your phone... %ld:%02ld";
constexpr const char *kPairDone = "Paired! The display now knows when you're here.";
constexpr const char *kPairClosed = "Pairing closed. Try again if your phone didn't show it.";
constexpr const char *kPairOff = "Turn Phone presence on first.";
constexpr const char *kStatusOff = "Off";
constexpr const char *kStatusNone = "No phone paired";
constexpr const char *kStatusNear = "%d paired \xC2\xB7 here now";
constexpr const char *kStatusAway = "%d paired \xC2\xB7 away %s";
constexpr const char *kStatusNever = "%d paired \xC2\xB7 not seen yet";
constexpr long kWelcomeAfter = 15 * 60;  // A: only after a real absence
constexpr uint32_t kCheckMs = 1000;
constexpr uint32_t kPairWindowMs = 120000;

// ---- State -------------------------------------------------------------------

PhoneState state = PhoneState::Off;
time_t awaySince = 0;
std::map<String, int64_t> eventsAtLeave;  // key -> start
std::set<String> tasksAtLeave;
uint32_t lastCheck = 0;
lv_obj_t *welcome = nullptr;
lv_obj_t *pairLayer = nullptr, *pairStatus = nullptr;
uint32_t pairStarted = 0;
int bondsBefore = 0;

// ---- Code --------------------------------------------------------------------

String duration(long secs) {
  const int m = (int)(secs / 60);
  return m >= 60 ? strf(kHoursMin, m / 60, m % 60) : strf(kMinutes, std::max(m, 1));
}

String taskKey(const Task &t) { return String((int)t.account) + ":" + t.id; }

void rememberWhatIsThere() {
  eventsAtLeave.clear();
  tasksAtLeave.clear();
  for (const Event &e : g_snap.events) eventsAtLeave[e.key] = e.start;
  for (const Task &t : g_snap.tasks) tasksAtLeave.insert(taskKey(t));
}

lv_obj_t *line(lv_obj_t *parent, const char *icon, uint32_t color, const String &text) {
  const Theme &t = theme();
  lv_obj_t *row = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  set_flex(row, LV_FLEX_FLOW_ROW, 12);
  lv_obj_t *i = mk_label(row, &font_16, color, icon);
  lv_obj_set_width(i, 22);
  mk_label(row, &font_16, t.text, text.c_str());
  return row;
}

// A: what changed while you were away.
void showWelcome(long awaySecs) {
  close_overlay(&welcome);
  const Theme &t = theme();
  int added = 0, moved = 0, gone = 0, newTasks = 0;
  for (const Event &e : g_snap.events) {
    auto it = eventsAtLeave.find(e.key);
    if (it == eventsAtLeave.end()) added++;
    else if (it->second != e.start) moved++;
  }
  std::set<String> now;
  for (const Task &task : g_snap.tasks) {
    now.insert(taskKey(task));
    newTasks += !tasksAtLeave.count(taskKey(task));
  }
  for (const String &k : tasksAtLeave) gone += !now.count(k);

  mk_overlay(true, &welcome);
  lv_obj_t *card = mk_box(welcome, t.card, 24);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(card, 560);
  lv_obj_set_style_pad_all(card, 26, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 12, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);
  mk_label(card, &font_34b, t.text, kWelcome);
  mk_label(card, &font_14, t.muted, strf(kAwayFor, duration(awaySecs).c_str()).c_str());
  lv_obj_t *list = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_ver(list, 6, 0);
  set_flex(list, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  if (added) line(list, ICON_CALENDAR, t.accent, added == 1 ? String(kNewMeeting) : strf(kNewMeetings, added));
  if (moved) line(list, ICON_CLOCK, t.warnFg, moved == 1 ? String(kMovedOne) : strf(kMoved, moved));
  if (gone) line(list, ICON_CHECK, t.good, gone == 1 ? String(kTaskGone) : strf(kTasksGone, gone));
  if (newTasks) line(list, ICON_PLUS, t.accent, newTasks == 1 ? String(kNewTask) : strf(kNewTasks, newTasks));
  if (!added && !moved && !gone && !newTasks) line(list, ICON_CHECK, t.good, kNothingNew);

  lv_obj_t *next = mk_box(card, t.raised, 14);
  lv_obj_set_width(next, 508);
  lv_obj_set_style_pad_all(next, 14, 0);
  set_flex(next, LV_FLEX_FLOW_COLUMN, 4, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *h = mk_label(next, &font_12, t.muted, kNext);
  lv_obj_set_style_text_letter_space(h, 1, 0);
  const time_t nowT = ui_now();
  const Event *upcoming = nullptr;
  for (const Event &e : g_snap.events) {
    if (!e.allDay && e.start / 1000 > nowT && day_number(e.start / 1000) == day_number(nowT)) {
      upcoming = &e;
      break;
    }
  }
  String nextText = upcoming ? fmt_time(upcoming->start / 1000) + "  " + upcoming->title : String(kNoNext);
  lv_obj_t *nl = mk_label(next, &font_16s, t.text, nextText.c_str());
  lv_obj_set_width(nl, 480);
  lv_label_set_long_mode(nl, LV_LABEL_LONG_DOT);
  if (upcoming) mk_label(next, &font_12, t.accent, until_text(upcoming->start).c_str());

  lv_obj_t *row = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_size(row, 508, 56);
  set_flex(row, LV_FLEX_FLOW_ROW, 10);
  lv_obj_t *show = mk_text_button(row, kShowDay, true, 250);
  lv_obj_set_height(show, 56);
  on_click(show, [](lv_event_t *) {
    lv_async_call([](void *) {
      close_overlay(&welcome);
      ui_show(Screen::Home);
    }, nullptr);
  });
  lv_obj_t *close = mk_text_button(row, kClose, false, 248);
  lv_obj_set_height(close, 56);
  on_click(close, [](lv_event_t *) { lv_async_call([](void *) { close_overlay(&welcome); }, nullptr); });
}

void updatePair() {
  if (!pairLayer || !pairStatus) return;
  const Theme &t = theme();
  if (presence_paired_count() > bondsBefore) {
    lv_label_set_text(pairStatus, kPairDone);
    lv_obj_set_style_text_color(pairStatus, c(t.good), 0);
  } else if (!presence_pairing()) {
    lv_label_set_text(pairStatus, kPairClosed);
    lv_obj_set_style_text_color(pairStatus, c(t.muted), 0);
  } else {
    const long left = std::max<long>(0, (long)(kPairWindowMs - (millis() - pairStarted)) / 1000);
    lv_label_set_text(pairStatus, strf(kPairWaiting, left / 60, left % 60).c_str());
  }
}

}  // namespace

bool presence_private() {
  return g_settings.presence && g_settings.layout(Layout::Presence) == 1 && state == PhoneState::Away;
}

bool presence_away() { return g_settings.presence && state == PhoneState::Away; }

bool presence_away_screen() {
  return g_settings.presence && g_settings.layout(Layout::Presence) == 2 && state == PhoneState::Away;
}

String event_title(const Event &ev) { return presence_private() ? String(kBusy) : ev.title; }

String presence_status() {
  if (!g_settings.presence) return kStatusOff;
  const int n = presence_paired_count();
  if (!n) return kStatusNone;
  const long ago = presence_seen_ago();
  if (state == PhoneState::Near) return strf(kStatusNear, n);
  if (ago < 0) return strf(kStatusNever, n);
  return strf(kStatusAway, n, duration(ago).c_str());
}

void presence_pair_show() {
  close_overlay(&pairLayer);
  const Theme &t = theme();
  mk_overlay(true, &pairLayer);
  lv_obj_t *card = mk_box(pairLayer, t.card, 24);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(card, 560);
  lv_obj_set_style_pad_all(card, 26, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 14, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);
  mk_label(card, &font_26b, t.text, kPairTitle);
  lv_obj_t *steps = mk_label(card, &font_16, t.text, kPairSteps);
  lv_obj_set_width(steps, 508);
  lv_label_set_long_mode(steps, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(steps, 6, 0);
  pairStatus = mk_label(card, &font_14b, t.accent, "");
  lv_obj_add_event_cb(pairStatus, [](lv_event_t *) { pairStatus = nullptr; }, LV_EVENT_DELETE, nullptr);
  lv_obj_t *close = mk_text_button(card, kClose, false, 508);
  lv_obj_set_height(close, 52);
  on_click(close, [](lv_event_t *) {
    lv_async_call([](void *) {
      presence_pair(false);
      close_overlay(&pairLayer);
      features_refresh();
    }, nullptr);
  });
  if (!g_settings.presence) {
    lv_label_set_text(pairStatus, kPairOff);
    return;
  }
  bondsBefore = presence_paired_count();
  pairStarted = millis();
  presence_pair(true);
  updatePair();
}

void presence_tick() {
  if (millis() - lastCheck < kCheckMs) return;
  lastCheck = millis();
  presence_apply(g_settings.presence);
  updatePair();
  const PhoneState was = state;
  state = presence_state(g_settings.awayMinutes);
  if (state == was) return;

  if (state == PhoneState::Away && was == PhoneState::Near) {
    const long ago = presence_seen_ago();
    awaySince = ui_now() - std::max<long>(ago, 0);
    rememberWhatIsThere();
  }
  if (state == PhoneState::Near && was == PhoneState::Away && awaySince) {
    const long away = (long)(ui_now() - awaySince);
    awaySince = 0;
    if (g_settings.presence && g_settings.layout(Layout::Presence) == 0 && away >= kWelcomeAfter) {
      if (ui_current() == Screen::Night) ui_show(Screen::Home);
      showWelcome(away);
    }
    lv_disp_trig_activity(nullptr);  // count as a touch: undim, leave the away screen
  }
  if (was == PhoneState::Away || state == PhoneState::Away) ui_refresh_all();  // B: titles
}
