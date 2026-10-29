// Tasks with filters by due date, account and list (mockup 4).
#include <algorithm>
#include <vector>

#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kAll = "All";
constexpr const char *kOverdue = "Overdue";
constexpr const char *kDueToday = "Due today";
constexpr const char *kAllDone = "All done!";
constexpr const char *kNothingHere = "Nothing here";
constexpr const char *kSeparator = " \xC2\xB7 ";  // between list, account and notes
constexpr const char *kAddTask = "Add task";
constexpr const char *kHoldHint = "Hold a task to start the focus timer";

constexpr int NAV_W = 200;
constexpr int BODY_Y = HEADER_H + GAP;
constexpr int BODY_H = SCREEN_H - BODY_Y - PAD;
constexpr int PANEL_W = SCREEN_W - 2 * PAD - NAV_W - GAP;

enum class Filter { All, Overdue, Today, List };

struct Selection {
  Filter filter = Filter::All;
  uint8_t account = 0;
  String list;
};

// ---- State -------------------------------------------------------------------

Selection sel;
lv_obj_t *nav, *panel;

// ---- Code --------------------------------------------------------------------

struct NavItem {
  Selection sel;
};

bool matches(const Task &t, const Selection &s) {
  Tone tone;
  switch (s.filter) {
    case Filter::All: return true;
    case Filter::Overdue: due_text(t.dueKey, tone); return tone == Tone::Bad;
    case Filter::Today: due_text(t.dueKey, tone); return tone == Tone::Warn;
    case Filter::List: return t.account == s.account && t.list == s.list;
  }
  return true;
}

bool same(const Selection &a, const Selection &b) {
  return a.filter == b.filter && (a.filter != Filter::List || (a.account == b.account && a.list == b.list));
}

void navButton(const char *label, int count, uint32_t dot, const Selection &s) {
  const Theme &t = theme();
  const bool on = same(s, sel);
  lv_obj_t *b = mk_button(nav, on ? t.raised : t.bg, 12);
  lv_obj_set_size(b, NAV_W, TOUCH);
  lv_obj_set_style_pad_hor(b, 12, 0);
  set_flex(b, LV_FLEX_FLOW_ROW, 8);
  mk_dot(b, dot);
  lv_obj_t *l = mk_label(b, &font_14b, on ? t.text : t.muted, label);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(l, 1);
  lv_obj_t *n = mk_label(b, &font_14, t.muted, "");
  lv_label_set_text_fmt(n, "%d", count);
  on_click(b, [](lv_event_t *e) {
    sel = static_cast<NavItem *>(lv_event_get_user_data(e))->sel;
    lv_async_call([](void *) { tasks_refresh(); }, nullptr);  // rebuilds the button being clicked
  }, attach(b, NavItem{s}));
}

void heading(const String &text) {
  lv_obj_t *h = mk_label(nav, &font_12, theme().muted, text.c_str());
  lv_obj_set_style_text_letter_space(h, 1, 0);
  lv_obj_set_style_pad_left(h, 12, 0);
  lv_obj_set_style_pad_top(h, 8, 0);
}

void taskRow(const Task &task, bool sub) {
  const Theme &t = theme();
  lv_obj_t *row = mk_box(panel, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_width(row, PANEL_W - 24);
  lv_obj_set_style_min_height(row, 54, 0);
  lv_obj_set_style_pad_left(row, sub ? 28 : 0, 0);
  set_flex(row, LV_FLEX_FLOW_ROW, 4);
  focus_hold(row, task);
  mk_check(row, task, 52);

  lv_obj_t *text = mk_box(row, 0, 0);
  lv_obj_set_style_bg_opa(text, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_grow(text, 1);
  set_flex(text, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *title = mk_label(text, &font_16, t.text, task.title.c_str());
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, PANEL_W - 24 - 56 - 110 - (sub ? 28 : 0));
  String meta = task.list;
  if (task.account < g_settings.accounts.size()) meta += kSeparator + g_settings.accounts[task.account].label;
  if (task.notes.length()) meta += kSeparator + task.notes;
  lv_obj_t *m = mk_label(text, &font_12, t.muted, meta.c_str());
  lv_label_set_long_mode(m, LV_LABEL_LONG_DOT);
  lv_obj_set_width(m, PANEL_W - 24 - 56 - 110 - (sub ? 28 : 0));

  Tone tone;
  const String due = due_text(task.dueKey, tone);
  if (due.length()) {
    lv_obj_t *chip = mk_box(row, tone == Tone::Bad ? t.badBg : tone == Tone::Warn ? t.warnBg : t.raised, 12);
    lv_obj_set_style_pad_hor(chip, 10, 0);
    lv_obj_set_style_pad_ver(chip, 4, 0);
    mk_label(chip, &font_12, tone == Tone::Bad ? t.badFg : tone == Tone::Warn ? t.warnFg : t.text, due.c_str());
  }
}

}  // namespace

lv_obj_t *tasks_create() {
  const Theme &t = theme();
  lv_obj_t *scr = mk_screen();
  mk_nav_header(scr, Screen::Tasks);

  nav = mk_scroll_column(scr, 4);
  lv_obj_set_pos(nav, PAD, BODY_Y);
  lv_obj_set_size(nav, NAV_W, BODY_H);

  panel = mk_scroll_column(scr, 2);
  lv_obj_set_style_bg_color(panel, c(t.panel), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(panel, 16, 0);
  lv_obj_set_style_pad_all(panel, 10, 0);
  lv_obj_set_pos(panel, PAD + NAV_W + GAP, BODY_Y);
  lv_obj_set_size(panel, PANEL_W, BODY_H);
  return scr;
}

void tasks_refresh() {
  const Theme &t = theme();
  lv_obj_clean(nav);
  lv_obj_clean(panel);

  int overdue = 0, today = 0;
  for (const Task &task : g_snap.tasks) {
    Tone tone;
    due_text(task.dueKey, tone);
    overdue += tone == Tone::Bad;
    today += tone == Tone::Warn;
  }
  if (g_settings.quickAdd) {
    lv_obj_t *add = mk_button(nav, t.accent, 12);
    lv_obj_set_size(add, NAV_W, TOUCH);
    set_flex(add, LV_FLEX_FLOW_ROW, 8, LV_FLEX_ALIGN_CENTER);
    mk_label(add, &font_14b, t.onAccent, ICON_PLUS);
    mk_label(add, &font_14b, t.onAccent, kAddTask);
    on_click(add, [](lv_event_t *) { quick_add_show(); });
  }
  navButton(kAll, g_snap.tasks.size(), t.text, Selection{Filter::All, 0, ""});
  navButton(kOverdue, overdue, t.badFg, Selection{Filter::Overdue, 0, ""});
  navButton(kDueToday, today, t.warnFg, Selection{Filter::Today, 0, ""});
  for (uint8_t a = 0; a < g_settings.accounts.size(); a++) {
    std::vector<String> lists;
    for (const Task &task : g_snap.tasks) {
      if (task.account == a && std::find(lists.begin(), lists.end(), task.list) == lists.end()) lists.push_back(task.list);
    }
    if (lists.empty()) continue;
    String name = g_settings.accounts[a].label;
    name.toUpperCase();
    heading(name);
    for (const String &list : lists) {
      int n = 0;
      for (const Task &task : g_snap.tasks) n += task.account == a && task.list == list;
      navButton(list.c_str(), n, g_settings.accounts[a].color, Selection{Filter::List, a, list});
    }
  }

  // Parents in due order, each followed by its subtasks.
  int shown = 0;
  std::vector<bool> used(g_snap.tasks.size(), false);
  for (size_t i = 0; i < g_snap.tasks.size(); i++) {
    const Task &p = g_snap.tasks[i];
    if (p.parent.length() || !matches(p, sel)) continue;
    taskRow(p, false);
    used[i] = true;
    shown++;
    for (size_t j = 0; j < g_snap.tasks.size(); j++) {
      const Task &ch = g_snap.tasks[j];
      if (ch.account == p.account && ch.parent == p.id) {
        taskRow(ch, true);
        used[j] = true;
      }
    }
  }
  for (size_t j = 0; j < g_snap.tasks.size(); j++) {
    if (!used[j] && g_snap.tasks[j].parent.length() && matches(g_snap.tasks[j], sel)) {
      taskRow(g_snap.tasks[j], false);
      shown++;
    }
  }
  if (shown && g_settings.focus) {
    lv_obj_t *hint = mk_label(panel, &font_12, t.muted, kHoldHint);
    lv_obj_set_style_pad_all(hint, 10, 0);
  }
  if (!shown) {
    lv_obj_t *l = mk_label(panel, &font_16, t.muted, sel.filter == Filter::All ? kAllDone : kNothingHere);
    lv_obj_set_style_pad_all(l, 16, 0);
  }
}
