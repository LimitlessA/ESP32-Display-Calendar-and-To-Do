// Smart desk settings: address, login and a connection test, typed on the
// on-screen keyboard. Opens from Settings > Features > Smart desk, and the
// Accounts screen can call desk_settings_show() too.
#include "desk.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kTitle = "Smart desk";
constexpr const char *kDefaultHost = "smartdesk-a43c.local";
constexpr const char *kHostLabel = "Address";
constexpr const char *kUserLabel = "Username";
constexpr const char *kPassLabel = "Password";
constexpr const char *kHostHint = "smartdesk-a43c.local or an IP address";
constexpr const char *kUserHint = "Empty = guest";
constexpr const char *kPassHint = "Password";
constexpr const char *kHint = "Leave the username empty for guest mode: memory buttons and Stop only. Raise, lower and the timer need a desk account.";
constexpr const char *kTest = "Test connection";
constexpr const char *kTesting = "Testing...";
constexpr const char *kSave = "Save";
constexpr const char *kCancel = "Cancel";
constexpr const char *kSaved = "Desk settings saved";
constexpr int kMaxLen = 63;
#define ROW_H sz(44)
constexpr int LABEL_W = 110;
#define FORM_W (SCREEN_W - 2 * PAD)

// ---- State -------------------------------------------------------------------

lv_obj_t *layer = nullptr;
lv_obj_t *fields[3] = {};  // address, username, password
lv_obj_t *keyboard = nullptr;
lv_obj_t *result = nullptr;
lv_timer_t *pollTimer = nullptr;

// ---- Code --------------------------------------------------------------------

String trimmed(lv_obj_t *ta) {
  String s = lv_textarea_get_text(ta);
  s.trim();
  return s;
}

void focusField(lv_obj_t *ta) {
  for (lv_obj_t *f : fields) lv_obj_clear_state(f, LV_STATE_FOCUSED);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);
  lv_keyboard_set_textarea(keyboard, ta);
}

void stopPolling() {
  if (pollTimer) lv_timer_del(pollTimer);
  pollTimer = nullptr;
}

void showResult(const String &text, uint32_t color) {
  if (!result) return;
  lv_label_set_text(result, text.c_str());
  lv_obj_set_style_text_color(result, c(color), 0);
}

void startTest() {
  const Theme &t = theme();
  desk_test(trimmed(fields[0]).length() ? trimmed(fields[0]) : String(kDefaultHost), trimmed(fields[1]),
            String(lv_textarea_get_text(fields[2])));
  showResult(kTesting, t.muted);
  stopPolling();
  pollTimer = lv_timer_create([](lv_timer_t *) {
    const DeskTestResult r = desk_test_result();
    if (r.running || !r.done) return;
    const Theme &t = theme();
    showResult(r.message, r.ok ? t.good : t.badFg);
    stopPolling();
  }, 300, nullptr);
}

void save() {
  Settings s = g_settings;
  s.deskHost = trimmed(fields[0]);
  if (!s.deskHost.length()) s.deskHost = kDefaultHost;
  s.deskUser = trimmed(fields[1]);
  s.deskPassword = s.deskUser.length() ? String(lv_textarea_get_text(fields[2])) : String("");
  ui_save_settings(s);
  toast(kSaved);
  close_overlay(&layer);
}

lv_obj_t *fieldRow(lv_obj_t *col, const char *label, const char *placeholder, const String &value, int taW) {
  const Theme &t = theme();
  lv_obj_t *row = mk_box(col, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_size(row, FORM_W, ROW_H);
  set_flex(row, LV_FLEX_FLOW_ROW, 8);
  lv_obj_t *l = mk_label(row, F_14B, t.muted, label);
  lv_obj_set_width(l, LABEL_W);
  lv_obj_t *ta = mk_textarea(row, placeholder);
  lv_obj_set_size(ta, taW, ROW_H);
  lv_obj_set_style_pad_ver(ta, 9, 0);
  lv_textarea_set_max_length(ta, kMaxLen);
  lv_textarea_set_text(ta, value.c_str());
  on_click(ta, [](lv_event_t *e) { focusField(lv_event_get_target(e)); });
  return ta;
}

}  // namespace

void desk_settings_show() {
  close_overlay(&layer);
  stopPolling();
  const Theme &t = theme();
  mk_overlay(false, &layer);
  lv_obj_add_event_cb(layer, [](lv_event_t *e) {
    // An older form, deleted later (lv_obj_del_async) while a new one is open,
    // must not clear the new one's fields: the keyboard then pointed nowhere.
    if (layer && layer != lv_event_get_target(e)) return;
    stopPolling();
    result = keyboard = nullptr;
    for (lv_obj_t *&f : fields) f = nullptr;
  }, LV_EVENT_DELETE, nullptr);

  lv_obj_t *head = mk_box(layer, 0, 0);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_size(head, FORM_W, HEADER_H);
  lv_obj_set_pos(head, PAD, 0);
  set_flex(head, LV_FLEX_FLOW_ROW, 10);
  lv_obj_t *title = mk_label(head, F_20B, t.text, kTitle);
  lv_obj_set_flex_grow(title, 1);
  on_click(mk_text_button(head, kCancel, false, 110), [](lv_event_t *) {
    lv_async_call([](void *) { close_overlay(&layer); }, nullptr);
  });
  on_click(mk_text_button(head, kSave, true, 110), [](lv_event_t *) {
    lv_async_call([](void *) { save(); }, nullptr);
  });

  lv_obj_t *col = mk_box(layer, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_size(col, FORM_W, 4 * ROW_H + 3 * 6);
  lv_obj_set_pos(col, PAD, HEADER_H + 2);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 6, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  const int taW = FORM_W - LABEL_W - 8;
  fields[0] = fieldRow(col, kHostLabel, kHostHint, g_settings.deskHost, taW);
  fields[1] = fieldRow(col, kUserLabel, kUserHint, g_settings.deskUser, taW);
  fields[2] = fieldRow(col, kPassLabel, kPassHint, g_settings.deskPassword, taW - ROW_H - 8);
  lv_textarea_set_password_mode(fields[2], true);
  lv_obj_t *eye = mk_icon_button(lv_obj_get_parent(fields[2]), ICON_EYE, t.muted);
  on_click(eye, [](lv_event_t *) {
    lv_textarea_set_password_mode(fields[2], !lv_textarea_get_password_mode(fields[2]));
  });

  lv_obj_t *test = mk_box(col, 0, 0);
  lv_obj_set_style_bg_opa(test, LV_OPA_TRANSP, 0);
  lv_obj_set_size(test, FORM_W, ROW_H);
  set_flex(test, LV_FLEX_FLOW_ROW, 12);
  on_click(mk_text_button(test, kTest, false, LABEL_W + 60), [](lv_event_t *) { startTest(); });
  result = mk_label(test, F_12, t.muted, kHint);
  lv_obj_set_width(result, FORM_W - LABEL_W - 72);
  lv_label_set_long_mode(result, LV_LABEL_LONG_WRAP);

  const int kbY = HEADER_H + 2 + 4 * ROW_H + 3 * 6 + 8;
  keyboard = mk_keyboard(layer, fields[0]);
  lv_obj_set_pos(keyboard, PAD, kbY);
  lv_obj_set_size(keyboard, FORM_W, SCREEN_H - kbY - 6);
  // Enter moves to the next field, and after the password runs the test.
  lv_obj_add_event_cb(keyboard, [](lv_event_t *) {
    lv_obj_t *cur = lv_keyboard_get_textarea(keyboard);
    if (cur == fields[0]) focusField(fields[1]);
    else if (cur == fields[1]) focusField(fields[2]);
    else startTest();
  }, LV_EVENT_READY, nullptr);
  focusField(fields[0]);
}

// ---- Name a memory button ------------------------------------------------------

namespace {

constexpr const char *kLabelTitle = "Name for M%d";
constexpr const char *kLabelHint = "Empty = default";
constexpr int kLabelMax = 16;

lv_obj_t *labelLayer = nullptr;
lv_obj_t *labelField = nullptr;
uint8_t labelSlot = 0;

void saveLabel() {
  Settings s = g_settings;
  String v = lv_textarea_get_text(labelField);
  v.trim();
  s.deskLabel[labelSlot - 1] = v.substring(0, kLabelMax);
  ui_save_settings(s);
  close_overlay(&labelLayer);
}

}  // namespace

void desk_label_edit(uint8_t slot) {
  if (slot < 1 || slot > 4) return;
  close_overlay(&labelLayer);
  const Theme &t = theme();
  labelSlot = slot;
  mk_overlay(false, &labelLayer);
  lv_obj_add_event_cb(labelLayer, [](lv_event_t *e) {
    if (labelLayer && labelLayer != lv_event_get_target(e)) return;
    labelField = nullptr;
  }, LV_EVENT_DELETE, nullptr);

  lv_obj_t *head = mk_box(labelLayer, 0, 0);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_size(head, FORM_W, HEADER_H);
  lv_obj_set_pos(head, PAD, 0);
  set_flex(head, LV_FLEX_FLOW_ROW, 10);
  lv_obj_t *title = mk_label(head, F_20B, t.text, strf(kLabelTitle, slot).c_str());
  lv_obj_set_flex_grow(title, 1);
  on_click(mk_text_button(head, kCancel, false, 110), [](lv_event_t *) {
    lv_async_call([](void *) { close_overlay(&labelLayer); }, nullptr);
  });
  on_click(mk_text_button(head, kSave, true, 110), [](lv_event_t *) {
    lv_async_call([](void *) { saveLabel(); }, nullptr);
  });

  labelField = mk_textarea(labelLayer, kLabelHint);
  lv_obj_set_size(labelField, FORM_W, ROW_H);
  lv_obj_set_pos(labelField, PAD, HEADER_H + 2);
  lv_obj_set_style_pad_ver(labelField, 9, 0);
  lv_textarea_set_max_length(labelField, kLabelMax);
  lv_textarea_set_text(labelField, g_settings.deskLabel[slot - 1].c_str());

  const int kbY = HEADER_H + 2 + ROW_H + 8;
  lv_obj_t *kb = mk_keyboard(labelLayer, labelField);
  lv_obj_set_pos(kb, PAD, kbY);
  lv_obj_set_size(kb, FORM_W, SCREEN_H - kbY - 6);
  lv_obj_add_event_cb(kb, [](lv_event_t *) { lv_async_call([](void *) { saveLabel(); }, nullptr); }, LV_EVENT_READY, nullptr);
}
