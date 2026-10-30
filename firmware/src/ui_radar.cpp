// Procrastination radar (mockup row "pr"): remembers how often each task was
// pushed back. A: stuck tasks glow with a move count, B: a nudge popup once a
// day, C: a radar view from the Tasks screen.
#include <math.h>

#include <algorithm>
#include <map>

#include "storage.h"
#include "sync.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kFile = "/radar.txt";
constexpr const char *kBadge = "%d\xC3\x97";  // 6x
constexpr const char *kMovedSince = "Moved %d times since %s";
constexpr const char *kNudgeHead = "YOU KEEP MOVING THIS ONE";
constexpr const char *kNudgeBody = "Moved %d times since %s. What would help?";
constexpr const char *kDo5 = "Do 5 minutes now";
constexpr const char *kSplit = "Split it";
constexpr const char *kFixDay = "Pick a fixed day";
constexpr const char *kDrop = "Drop it";
constexpr const char *kLater = "Not now";
constexpr const char *kTomorrow = "Tomorrow";
constexpr const char *kSaturday = "Saturday";
constexpr const char *kNextMonday = "Next Monday";
constexpr const char *kSplitHint = "Add the first small step as its own task";
constexpr const char *kFixed = "Fixed for %s. This one won't count as a move.";
constexpr const char *kDropped = "Dropped: %s";
constexpr const char *kUndo = "Undo";
constexpr const char *kRadarTitle = "Procrastination radar";
constexpr const char *kRadarHint = "Closer to the middle = moved more often. Tap one to deal with it.";
constexpr const char *kRadarEmpty = "Nothing stuck. Tasks you keep moving show up here.";
constexpr const char *kStuck = "STUCK";
constexpr const char *kClose = "Close";
constexpr uint32_t kNudgeIdleMs = 60000;     // B: only when nobody has touched the screen for a minute
constexpr uint32_t kNudgeCheckMs = 30000;
constexpr long kForgetAfterDays = 14;        // finished tasks leave the file after this
constexpr int RADAR = 380;                   // C: diameter
const uint32_t kBubbleColors[] = {0xF5A524, 0xFF8A65, 0xE57CD8, 0x6C8CFF, 0x4FD1C5, 0x7BD88F};

struct Entry {
  uint16_t moves = 0;
  time_t firstSeen = 0;
  String lastDue;     // due date when last seen
  String countedDue;  // due date already counted as passed
  long lastSeenDay = 0;
  long nudgedDay = 0;
};

// ---- State -------------------------------------------------------------------

std::map<String, Entry> entries;
bool loaded = false;
uint32_t lastNudgeCheck = 0;
Task popupTask;
Task undoTask;
lv_obj_t *popup = nullptr;
lv_obj_t *view = nullptr;

// ---- Code --------------------------------------------------------------------

String keyOf(const Task &t) { return String((int)t.account) + ":" + t.id; }

void load() {
  loaded = true;
  String text;
  if (!storage_read(kFile, text)) return;
  int pos = 0;
  while (pos < (int)text.length()) {
    int end = text.indexOf('\n', pos);
    if (end < 0) end = text.length();
    const String line = text.substring(pos, end);
    pos = end + 1;
    String f[7];
    int from = 0;
    for (int i = 0; i < 7; i++) {
      const int tab = line.indexOf('\t', from);
      f[i] = tab < 0 ? line.substring(from) : line.substring(from, tab);
      if (tab < 0) break;
      from = tab + 1;
    }
    if (!f[0].length()) continue;
    Entry e;
    e.moves = f[1].toInt();
    e.firstSeen = f[2].toInt();
    e.lastDue = f[3];
    e.countedDue = f[4];
    e.lastSeenDay = f[5].toInt();
    e.nudgedDay = f[6].toInt();
    entries[f[0]] = e;
  }
}

void save() {
  String text;
  for (const auto &kv : entries) {
    const Entry &e = kv.second;
    text += kv.first + "\t" + String((int)e.moves) + "\t" + String((long)e.firstSeen) + "\t" + e.lastDue + "\t" +
            e.countedDue + "\t" + String(e.lastSeenDay) + "\t" + String(e.nudgedDay) + "\n";
  }
  storage_write(kFile, text);
}

const Entry *find(const Task &t) {
  auto it = entries.find(keyOf(t));
  return it == entries.end() ? nullptr : &it->second;
}

String since(const Entry &e) {
  return fmt_date_short(e.firstSeen).substring(4);  // "2 Oct"
}

bool stuck(const Entry *e) { return e && e->moves >= g_settings.radarMoves; }

// The next such weekday, a week ahead when it is today (0 = Sunday).
time_t nextWeekday(int wday) {
  const time_t now = ui_now();
  struct tm tm;
  localtime_r(&now, &tm);
  int add = (wday - tm.tm_wday + 7) % 7;
  if (add == 0) add = 7;
  return day_start(now) + add * 86400L + 3600;  // an hour in, safe across DST
}

void closePopup() { close_overlay(&popup); }

void fixDay(time_t day) {
  const String key = day_key(day);
  auto it = entries.find(keyOf(popupTask));
  if (it != entries.end()) it->second.lastDue = key;  // a promise, not a move
  save();
  sync_set_task_due(popupTask, key);
  toast(strf(kFixed, fmt_date_short(day).c_str()));
  closePopup();
}

lv_obj_t *popupButton(lv_obj_t *parent, const char *text, bool primary, lv_event_cb_t cb) {
  lv_obj_t *b = mk_text_button(parent, text, primary, 230);
  lv_obj_set_height(b, 52);
  on_click(b, cb);
  return b;
}

void showDays(lv_event_t *) {
  lv_async_call([](void *) {
    if (!popup) return;
    lv_obj_t *card = lv_obj_get_child(popup, 0);
    lv_obj_t *buttons = lv_obj_get_child(card, lv_obj_get_child_cnt(card) - 1);
    lv_obj_clean(buttons);
    popupButton(buttons, kTomorrow, true, [](lv_event_t *) {
      lv_async_call([](void *) { fixDay(ui_now() + 86400); }, nullptr);
    });
    popupButton(buttons, kSaturday, false, [](lv_event_t *) {
      lv_async_call([](void *) { fixDay(nextWeekday(6)); }, nullptr);
    });
    popupButton(buttons, kNextMonday, false, [](lv_event_t *) {
      lv_async_call([](void *) { fixDay(nextWeekday(1)); }, nullptr);
    });
    popupButton(buttons, kLater, false, [](lv_event_t *) { lv_async_call([](void *) { closePopup(); }, nullptr); });
  }, nullptr);
}

}  // namespace

void radar_update() {
  if (!loaded) load();
  if (!g_settings.radar || !clock_valid()) return;
  const time_t now = ui_now();
  const long today = day_number(now);
  const String todayKey = day_key(now);
  bool changed = false;
  for (const Task &t : g_snap.tasks) {
    auto it = entries.find(keyOf(t));
    if (it == entries.end()) {
      Entry e;
      e.firstSeen = now;
      e.lastDue = t.dueKey;
      e.lastSeenDay = today;
      entries[keyOf(t)] = e;
      changed = true;
      continue;
    }
    Entry &e = it->second;
    if (e.lastSeenDay != today) {
      e.lastSeenDay = today;
      changed = true;
    }
    // Pushed to a later day, unless that day was already counted as missed.
    if (t.dueKey.length() && e.lastDue.length() && strcmp(t.dueKey.c_str(), e.lastDue.c_str()) > 0) {
      if (e.countedDue != e.lastDue) e.moves++;
      changed = true;
    }
    if (t.dueKey != e.lastDue) {
      e.lastDue = t.dueKey;
      changed = true;
    }
    // A due day went by and the task is still open.
    if (t.dueKey.length() && strcmp(t.dueKey.c_str(), todayKey.c_str()) < 0 && e.countedDue != t.dueKey) {
      e.moves++;
      e.countedDue = t.dueKey;
      changed = true;
    }
  }
  for (auto it = entries.begin(); it != entries.end();) {
    if (today - it->second.lastSeenDay > kForgetAfterDays) {
      it = entries.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  if (changed) save();
}

int radar_moves(const Task &t) {
  const Entry *e = find(t);
  return e ? e->moves : 0;
}

bool radar_stuck(const Task &t) { return g_settings.radar && stuck(find(t)); }

// A: an orange glow and a move count on the task card.
String radar_mark(lv_obj_t *card, const Task &t) {
  if (!g_settings.radar || g_settings.layout(Layout::Radar) != 0) return "";
  const Entry *e = find(t);
  if (!stuck(e)) return "";
  const Theme &th = theme();
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_border_color(card, c(th.accent), 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_shadow_color(card, c(th.accent), 0);
  lv_obj_set_style_shadow_width(card, 18, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_t *badge = mk_box(card, th.accent, 10);
  lv_obj_set_style_pad_hor(badge, 8, 0);
  lv_obj_set_style_pad_ver(badge, 2, 0);
  mk_label(badge, &font_12, th.onAccent, strf(kBadge, (int)e->moves).c_str());
  return strf(kMovedSince, (int)e->moves, since(*e).c_str());
}

// B, and a tap in C: what to do about a task you keep moving.
void radar_nudge_show(const Task &t) {
  const Entry *e = find(t);
  if (!e) return;
  closePopup();
  popupTask = t;
  const Theme &th = theme();
  mk_overlay(true, &popup);
  lv_obj_t *card = mk_box(popup, th.card, 22);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(card, 540);
  lv_obj_set_style_pad_all(card, 24, 0);
  lv_obj_set_style_border_color(card, c(th.accent), 0);
  lv_obj_set_style_border_width(card, 2, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);
  lv_obj_t *head = mk_label(card, &font_12, th.accent, kNudgeHead);
  lv_obj_set_style_text_letter_space(head, 1, 0);
  lv_obj_t *title = mk_label(card, &font_26b, th.text, t.title.c_str());
  lv_obj_set_width(title, 492);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  mk_label(card, &font_14, th.muted, strf(kNudgeBody, (int)e->moves, since(*e).c_str()).c_str());

  lv_obj_t *buttons = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
  lv_obj_set_width(buttons, 492);
  lv_obj_set_style_pad_top(buttons, 8, 0);
  lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(buttons, 10, 0);
  lv_obj_set_style_pad_column(buttons, 10, 0);
  if (g_settings.focus) {
    popupButton(buttons, kDo5, true, [](lv_event_t *) {
      lv_async_call([](void *) {
        closePopup();
        focus_start(popupTask, 5);
      }, nullptr);
    });
  }
  if (g_settings.quickAdd) {
    popupButton(buttons, kSplit, !g_settings.focus, [](lv_event_t *) {
      lv_async_call([](void *) {
        closePopup();
        quick_add_show();
        toast(kSplitHint);
      }, nullptr);
    });
  }
  popupButton(buttons, kFixDay, false, showDays);
  popupButton(buttons, kDrop, false, [](lv_event_t *) {
    lv_async_call([](void *) {
      undoTask = popupTask;
      closePopup();
      sync_complete_task(undoTask);
      toast(strf(kDropped, undoTask.title.c_str()), kUndo,
            [](lv_event_t *) { sync_uncomplete_task(undoTask); }, nullptr);
    }, nullptr);
  });
}

void radar_tick() {
  if (!g_settings.radar || g_settings.layout(Layout::Radar) != 1) return;
  if (millis() - lastNudgeCheck < kNudgeCheckMs) return;
  lastNudgeCheck = millis();
  if (!clock_valid() || overlay_visible() || ui_current() != Screen::Home) return;
  if (lv_disp_get_inactive_time(nullptr) < kNudgeIdleMs) return;
  const long today = day_number(ui_now());
  const Task *best = nullptr;
  int bestMoves = 0;
  for (const Task &t : g_snap.tasks) {
    const Entry *e = find(t);
    if (stuck(e) && e->nudgedDay != today && e->moves > bestMoves) {
      best = &t;
      bestMoves = e->moves;
    }
  }
  if (!best) return;
  entries[keyOf(*best)].nudgedDay = today;
  save();
  radar_nudge_show(*best);
}

bool radar_view_on() { return g_settings.radar && g_settings.layout(Layout::Radar) == 2; }

// C: tasks as bubbles on a radar; the more moves, the closer to the middle.
void radar_view_show() {
  close_overlay(&view);
  const Theme &th = theme();
  mk_overlay(false, &view);
  lv_obj_set_style_bg_color(view, c(th.bg), 0);
  lv_obj_set_style_bg_opa(view, LV_OPA_COVER, 0);
  lv_obj_add_flag(view, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *radar = mk_box(view, th.panel, RADAR / 2);
  lv_obj_set_size(radar, RADAR, RADAR);
  lv_obj_set_pos(radar, 30, (SCREEN_H - RADAR) / 2);
  lv_obj_clear_flag(radar, LV_OBJ_FLAG_SCROLLABLE);
  for (int r = 1; r <= 3; r++) {
    const int d = RADAR * r / 4;
    lv_obj_t *ring = mk_box(radar, 0, d / 2);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, c(r == 1 ? th.accent : th.line), 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_size(ring, d, d);
    lv_obj_center(ring);
  }
  lv_obj_t *mid = mk_label(radar, &font_12, th.accent, kStuck);
  lv_obj_set_style_text_letter_space(mid, 1, 0);
  lv_obj_center(mid);

  std::vector<const Task *> list;
  for (const Task &t : g_snap.tasks) {
    if (radar_moves(t) > 0) list.push_back(&t);
  }
  std::sort(list.begin(), list.end(), [](const Task *a, const Task *b) { return radar_moves(*a) > radar_moves(*b); });
  if (list.size() > 10) list.resize(10);
  const int maxMoves = g_settings.radarMoves * 2;
  for (size_t i = 0; i < list.size(); i++) {
    const int moves = radar_moves(*list[i]);
    const float closeness = std::min(1.0f, (float)moves / maxMoves);
    const float dist = (RADAR / 2 - 30) * (1.0f - closeness) + 26;
    const float angle = (float)i * 2.39996f;  // golden angle, spreads the bubbles
    const int size = 26 + std::min(moves, 8) * 3;
    lv_obj_t *b = mk_button(radar, kBubbleColors[i % 6], size / 2);
    lv_obj_set_size(b, size, size);
    lv_obj_align(b, LV_ALIGN_CENTER, (lv_coord_t)(dist * cosf(angle)), (lv_coord_t)(dist * sinf(angle)));
    lv_obj_t *n = mk_label(b, &font_12, 0x101317, String(moves).c_str());
    lv_obj_center(n);
    on_click(b, [](lv_event_t *e) { radar_nudge_show(*static_cast<Task *>(lv_event_get_user_data(e))); },
             attach(b, *list[i]));
  }

  const int x = 30 + RADAR + 30, w = SCREEN_W - x - 24;
  lv_obj_t *side = mk_box(view, 0, 0);
  lv_obj_set_style_bg_opa(side, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(side, x, 24);
  lv_obj_set_size(side, w, SCREEN_H - 48);
  set_flex(side, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  mk_label(side, &font_20b, th.text, kRadarTitle);
  lv_obj_t *hint = mk_label(side, &font_12, th.muted, list.empty() ? kRadarEmpty : kRadarHint);
  lv_obj_set_width(hint, w);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_t *rows = mk_scroll_column(side, 6);
  lv_obj_set_width(rows, w);
  lv_obj_set_flex_grow(rows, 1);
  for (size_t i = 0; i < list.size(); i++) {
    const Task &t = *list[i];
    lv_obj_t *row = mk_button(rows, th.card, 12);
    lv_obj_set_size(row, w, TOUCH);
    lv_obj_set_style_pad_hor(row, 10, 0);
    set_flex(row, LV_FLEX_FLOW_ROW, 8);
    mk_dot(row, kBubbleColors[i % 6], 10);
    lv_obj_t *l = mk_label(row, &font_14b, th.text, t.title.c_str());
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(l, 1);
    mk_label(row, &font_14b, radar_stuck(t) ? th.accent : th.muted, strf(kBadge, radar_moves(t)).c_str());
    on_click(row, [](lv_event_t *e) { radar_nudge_show(*static_cast<Task *>(lv_event_get_user_data(e))); },
             attach(row, t));
  }
  lv_obj_t *close = mk_text_button(side, kClose, false, w);
  on_click(close, [](lv_event_t *) { lv_async_call([](void *) { close_overlay(&view); }, nullptr); });
}
