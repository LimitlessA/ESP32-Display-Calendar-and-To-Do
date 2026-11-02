// Quick add task (mockup row 1): A full keyboard, B frequent task tiles,
// C a sheet with suggestions over the current screen.
#include "sync.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kCancel = "Cancel";
constexpr const char *kNewTask = "New task";
constexpr const char *kAddTask = "Add a task";
constexpr const char *kAdd = "Add";
constexpr const char *kAddNamed = "Add \"%s\"";
constexpr const char *kTypeOwn = "Type my own";
constexpr const char *kPlaceholder = "What needs doing?";
constexpr const char *kFrequent = "YOUR FREQUENT TASKS";
constexpr const char *kFrequentHint = "Learned from your Google Tasks history";
constexpr const char *kNoFrequent = "Your most frequent tasks show up here once you have completed a few.";
constexpr const char *kFromPast = "from past tasks";
constexpr const char *kDueLabel = "Due";
constexpr const char *kAdded = "Added: %s";
constexpr const char *kDueNames[] = {"Today", "Tomorrow", "This weekend", "No date"};
constexpr int kTiles = 12;        // 4 x 3 in layout B
constexpr int kSuggestions = 3;   // chips above the keyboard in layout C
constexpr int SHEET_H = 372;

enum class Due { Today, Tomorrow, Weekend, None, Count };

// ---- State -------------------------------------------------------------------

lv_obj_t *layer = nullptr;
lv_obj_t *input = nullptr;       // text area (A, C)
lv_obj_t *dueRow = nullptr;      // due chips (A, B)
lv_obj_t *dueCycle = nullptr;    // single due chip that cycles (C)
lv_obj_t *listChip = nullptr;
lv_obj_t *suggest = nullptr;     // suggestion row (C)
lv_obj_t *addNamed = nullptr;    // add button with the picked tile (B)
Due due = Due::Today;
size_t listIndex = 0;
String picked;                   // tile picked in B

// ---- Code --------------------------------------------------------------------

String dueKey(Due d) {
  const time_t now = ui_now();
  struct tm tm;
  localtime_r(&now, &tm);
  switch (d) {
    case Due::Today: return day_key(now);
    case Due::Tomorrow: return day_key(now + 86400);
    case Due::Weekend: {
      const int toSat = (6 - tm.tm_wday + 7) % 7;  // Saturday, or today if it is the weekend
      return day_key(now + (tm.tm_wday == 0 ? 0 : toSat) * 86400L);
    }
    default: return "";
  }
}

String listName() {
  if (g_snap.lists.empty()) return g_settings.accounts.empty() ? String("") : g_settings.accounts[0].label;
  const TaskList &l = g_snap.lists[listIndex % g_snap.lists.size()];
  if (g_settings.accounts.size() > 1 && l.account < g_settings.accounts.size()) {
    return l.title + " \xC2\xB7 " + g_settings.accounts[l.account].label;
  }
  return l.title;
}

uint32_t listColor() {
  if (g_snap.lists.empty() || g_settings.accounts.empty()) return theme().muted;
  const uint8_t a = g_snap.lists[listIndex % g_snap.lists.size()].account;
  return a < g_settings.accounts.size() ? g_settings.accounts[a].color : theme().muted;
}

void close() {
  close_overlay(&layer);
  input = dueRow = dueCycle = listChip = suggest = addNamed = nullptr;
}

void add(const String &raw) {
  String title = raw;
  title.trim();
  if (!title.length()) return;
  uint8_t account = 0;
  String listId;
  if (!g_snap.lists.empty()) {
    const TaskList &l = g_snap.lists[listIndex % g_snap.lists.size()];
    account = l.account;
    listId = l.id;
  }
  sync_add_task(account, listId, title, dueKey(due));
  close();
  toast(strf(kAdded, title.c_str()));
}

void addFromInput() {
  if (input) add(lv_textarea_get_text(input));
}

void refreshDue() {
  // Layout B removes "This weekend", so match each chip by the choice stored
  // in it, not by its position (child 0 is the "Due" label).
  if (dueRow) {
    const uint32_t n = lv_obj_get_child_cnt(dueRow);
    for (uint32_t i = 1; i < n; i++) {
      lv_obj_t *chip = lv_obj_get_child(dueRow, i);
      set_chip(chip, (Due)(intptr_t)lv_obj_get_user_data(chip) == due);
    }
  }
  if (dueCycle) lv_label_set_text(lv_obj_get_child(dueCycle, 0), kDueNames[(int)due]);
}

void mkDueChips(lv_obj_t *parent) {
  dueRow = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(dueRow, LV_OPA_TRANSP, 0);
  set_flex(dueRow, LV_FLEX_FLOW_ROW, 6);
  mk_label(dueRow, &font_14, theme().muted, kDueLabel);
  for (int i = 0; i < (int)Due::Count; i++) {
    lv_obj_t *chip = mk_chip(dueRow, kDueNames[i], (Due)i == due);
    lv_obj_set_user_data(chip, (void *)(intptr_t)i);
    on_click(chip, [](lv_event_t *e) {
      due = (Due)(intptr_t)lv_event_get_user_data(e);
      refreshDue();
    }, (void *)(intptr_t)i);
  }
}

void mkListChip(lv_obj_t *parent) {
  const Theme &t = theme();
  listChip = mk_button(parent, t.bg, 20);
  lv_obj_set_size(listChip, LV_SIZE_CONTENT, 40);
  lv_obj_set_style_min_height(listChip, 40, 0);
  lv_obj_set_style_pad_hor(listChip, 14, 0);
  lv_obj_set_style_border_color(listChip, c(t.line), 0);
  lv_obj_set_style_border_width(listChip, 2, 0);
  set_flex(listChip, LV_FLEX_FLOW_ROW, 8);
  mk_dot(listChip, listColor());
  mk_label(listChip, &font_14b, t.text, listName().c_str());
  on_click(listChip, [](lv_event_t *) {
    if (g_snap.lists.empty()) return;
    listIndex = (listIndex + 1) % g_snap.lists.size();
    lv_obj_set_style_bg_color(lv_obj_get_child(listChip, 0), c(listColor()), 0);
    lv_label_set_text(lv_obj_get_child(listChip, 1), listName().c_str());
  });
}

lv_obj_t *topBar(lv_obj_t *parent, const char *title) {
  const Theme &t = theme();
  lv_obj_t *bar = mk_box(parent, t.header, 0);
  lv_obj_set_size(bar, SCREEN_W, HEADER_H);
  lv_obj_set_style_pad_hor(bar, 10, 0);
  set_flex(bar, LV_FLEX_FLOW_ROW, 0, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *cancel = mk_button(bar, t.header, 22);
  lv_obj_set_size(cancel, LV_SIZE_CONTENT, TOUCH);
  lv_obj_set_style_pad_hor(cancel, 16, 0);
  lv_obj_center(mk_label(cancel, &font_16s, t.muted, kCancel));
  on_click(cancel, [](lv_event_t *) { close(); });
  mk_label(bar, &font_16s, t.text, title);
  return bar;
}

// A: input, list and due chips, then a full-width keyboard.
void buildKeyboard() {
  const Theme &t = theme();
  lv_obj_clean(layer);
  dueCycle = suggest = addNamed = nullptr;
  lv_obj_t *bar = topBar(layer, kNewTask);
  lv_obj_t *addBtn = mk_text_button(bar, kAdd, true, 96);
  on_click(addBtn, [](lv_event_t *) { addFromInput(); });

  lv_obj_t *form = mk_box(layer, 0, 0);
  lv_obj_set_style_bg_opa(form, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(form, PAD, HEADER_H + 10);
  lv_obj_set_width(form, SCREEN_W - 2 * PAD);
  set_flex(form, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  input = mk_textarea(form, kPlaceholder);
  lv_obj_set_width(input, SCREEN_W - 2 * PAD);
  lv_obj_t *chips = mk_box(form, 0, 0);
  lv_obj_set_style_bg_opa(chips, LV_OPA_TRANSP, 0);
  set_flex(chips, LV_FLEX_FLOW_ROW, 10);
  mkListChip(chips);
  mkDueChips(chips);

  const int kbY = HEADER_H + 10 + 48 + 8 + 40 + 10;
  lv_obj_t *kb = mk_keyboard(layer, input);
  lv_obj_set_pos(kb, 0, kbY);
  lv_obj_set_size(kb, SCREEN_W, SCREEN_H - kbY);
  lv_obj_set_style_radius(kb, 0, 0);
  lv_obj_set_style_bg_color(kb, c(t.header), 0);
  lv_obj_add_event_cb(kb, [](lv_event_t *) { addFromInput(); }, LV_EVENT_READY, nullptr);
}

void pickTile(lv_event_t *e) {
  lv_obj_t *tile = lv_event_get_target(e);
  lv_obj_t *grid = lv_obj_get_parent(tile);
  const Theme &t = theme();
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(grid); i++) {
    lv_obj_t *o = lv_obj_get_child(grid, i);
    lv_obj_set_style_border_color(o, c(o == tile ? t.accent : t.card), 0);
    lv_obj_set_style_bg_color(o, c(o == tile ? t.warnBg : t.card), 0);
  }
  picked = *static_cast<String *>(lv_event_get_user_data(e));
  String name = picked.length() > 24 ? picked.substring(0, 22) + "..." : picked;
  lv_label_set_text(lv_obj_get_child(addNamed, 0), strf(kAddNamed, name.c_str()).c_str());
  lv_obj_clear_state(addNamed, LV_STATE_DISABLED);
  lv_obj_set_style_bg_color(addNamed, c(t.accent), 0);
}

// B: tiles of the tasks you complete most often.
void buildTiles() {
  const Theme &t = theme();
  lv_obj_t *bar = topBar(layer, kAddTask);
  lv_obj_t *own = mk_button(bar, t.header, 22);
  lv_obj_set_size(own, LV_SIZE_CONTENT, TOUCH);
  lv_obj_set_style_pad_hor(own, 16, 0);
  lv_obj_set_style_border_color(own, c(t.line), 0);
  lv_obj_set_style_border_width(own, 2, 0);
  set_flex(own, LV_FLEX_FLOW_ROW, 8);
  mk_label(own, &font_16, t.text, ICON_KEYBOARD);
  mk_label(own, &font_14b, t.text, kTypeOwn);
  on_click(own, [](lv_event_t *) { lv_async_call([](void *) { buildKeyboard(); }, nullptr); });

  lv_obj_t *hint = mk_box(layer, 0, 0);
  lv_obj_set_style_bg_opa(hint, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(hint, PAD, HEADER_H + 10);
  lv_obj_set_width(hint, SCREEN_W - 2 * PAD);
  set_flex(hint, LV_FLEX_FLOW_ROW, 0, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *h = mk_label(hint, &font_12, t.muted, kFrequent);
  lv_obj_set_style_text_letter_space(h, 1, 0);
  mk_label(hint, &font_12, t.muted, kFrequentHint);

  const int gridY = HEADER_H + 34, footH = 64;
  const int gridH = SCREEN_H - gridY - footH - 10;
  lv_obj_t *grid = mk_box(layer, 0, 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(grid, PAD, gridY);
  lv_obj_set_size(grid, SCREEN_W - 2 * PAD, gridH);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, 8, 0);
  lv_obj_set_style_pad_column(grid, 8, 0);
  const int tw = (SCREEN_W - 2 * PAD - 3 * 8) / 4, th = (gridH - 2 * 8) / 3;
  int n = 0;
  for (const String &title : g_snap.frequent) {
    if (n++ >= kTiles) break;
    lv_obj_t *tile = mk_button(grid, t.card, 14);
    lv_obj_set_size(tile, tw, th);
    lv_obj_set_style_border_width(tile, 2, 0);
    lv_obj_set_style_border_color(tile, c(t.card), 0);
    lv_obj_set_style_pad_all(tile, 12, 0);
    lv_obj_t *l = mk_label(tile, &font_16s, t.text, title.c_str());
    lv_obj_set_width(l, tw - 24);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_add_event_cb(tile, pickTile, LV_EVENT_CLICKED, attach(tile, title));
  }
  if (!n) {
    lv_obj_t *l = mk_label(grid, &font_16, t.muted, kNoFrequent);
    lv_obj_set_width(l, SCREEN_W - 2 * PAD);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  }

  lv_obj_t *foot = mk_box(layer, t.header, 0);
  lv_obj_set_pos(foot, 0, SCREEN_H - footH);
  lv_obj_set_size(foot, SCREEN_W, footH);
  lv_obj_set_style_pad_hor(foot, 10, 0);
  set_flex(foot, LV_FLEX_FLOW_ROW, 6, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
  mkDueChips(foot);
  lv_obj_del(lv_obj_get_child(dueRow, 3));  // "This weekend" does not fit next to the add button
  addNamed = mk_text_button(foot, kAdd, true, LV_SIZE_CONTENT);
  lv_obj_set_height(addNamed, 48);
  lv_obj_set_style_bg_color(addNamed, c(t.raised), 0);
  lv_obj_add_state(addNamed, LV_STATE_DISABLED);
  on_click(addNamed, [](lv_event_t *) { add(picked); });
}

// Suggestions for what is typed so far, from frequent and open tasks.
void refreshSuggestions() {
  if (!suggest || !input) return;
  lv_obj_clean(suggest);
  String typed = lv_textarea_get_text(input);
  typed.toLowerCase();
  std::vector<String> found;
  auto consider = [&](const String &title) {
    if ((int)found.size() >= kSuggestions) return;
    String low = title;
    low.toLowerCase();
    if (typed.length() && !low.startsWith(typed)) return;
    for (const String &f : found) {
      if (f == title) return;
    }
    found.push_back(title);
  };
  for (const String &f : g_snap.frequent) consider(f);
  for (const Task &task : g_snap.tasks) consider(task.title);
  for (const String &f : found) {
    lv_obj_t *chip = mk_chip(suggest, f.c_str(), false);
    lv_obj_set_style_max_width(chip, 230, 0);
    lv_label_set_long_mode(lv_obj_get_child(chip, 0), LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(lv_obj_get_child(chip, 0), 200, 0);
    lv_obj_add_event_cb(chip, [](lv_event_t *e) {
      lv_textarea_set_text(input, static_cast<String *>(lv_event_get_user_data(e))->c_str());
    }, LV_EVENT_CLICKED, attach(chip, f));
  }
  if (found.size()) mk_label(suggest, &font_12, theme().muted, kFromPast);
}

// C: a sheet over the current screen.
void buildSheet() {
  const Theme &t = theme();
  lv_obj_t *sheet = mk_box(layer, t.panel, 20);
  lv_obj_set_pos(sheet, 0, SCREEN_H - SHEET_H);
  lv_obj_set_size(sheet, SCREEN_W, SHEET_H + 20);  // the extra hides the bottom corners
  lv_obj_set_style_pad_all(sheet, 10, 0);
  set_flex(sheet, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *row = mk_box(sheet, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_width(row, SCREEN_W - 20);
  set_flex(row, LV_FLEX_FLOW_ROW, 8);
  input = mk_textarea(row, kPlaceholder);
  lv_obj_set_flex_grow(input, 1);
  lv_obj_add_event_cb(input, [](lv_event_t *) { refreshSuggestions(); }, LV_EVENT_VALUE_CHANGED, nullptr);
  dueCycle = mk_chip(row, kDueNames[(int)due], false, 48);
  lv_obj_set_style_border_color(dueCycle, c(t.line), 0);
  lv_obj_set_style_border_width(dueCycle, 2, 0);
  on_click(dueCycle, [](lv_event_t *) {
    due = (Due)(((int)due + 1) % (int)Due::Count);
    refreshDue();
  });
  lv_obj_t *addBtn = mk_text_button(row, kAdd, true, 90);
  lv_obj_set_height(addBtn, 48);
  on_click(addBtn, [](lv_event_t *) { addFromInput(); });
  lv_obj_t *x = mk_icon_button(row, ICON_CLOSE, t.muted);
  on_click(x, [](lv_event_t *) { close(); });

  suggest = mk_box(sheet, 0, 0);
  lv_obj_set_style_bg_opa(suggest, LV_OPA_TRANSP, 0);
  lv_obj_set_size(suggest, SCREEN_W - 20, 40);
  set_flex(suggest, LV_FLEX_FLOW_ROW, 6);
  refreshSuggestions();

  lv_obj_t *kb = mk_keyboard(sheet, input);
  lv_obj_set_size(kb, SCREEN_W - 20, SHEET_H - 10 - 48 - 8 - 40 - 8 - 10);
  lv_obj_set_style_pad_all(kb, 0, 0);
  lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, 0);
  lv_obj_add_event_cb(kb, [](lv_event_t *) { addFromInput(); }, LV_EVENT_READY, nullptr);
}

}  // namespace

void quick_add_show() {
  if (!g_settings.quickAdd) return;
  close();
  due = Due::Today;
  picked = "";
  const uint8_t l = g_settings.layout(Layout::QuickAdd);
  mk_overlay(l == 2, &layer);
  if (l == 0) buildKeyboard();
  else if (l == 1) buildTiles();
  else buildSheet();
}
