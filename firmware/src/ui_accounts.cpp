// Google accounts: status, calendar choice, removal, and how to add one (mockup 8).
#include "sync.h"
#include "ui_internal.h"
#include "web.h"
#include "wifi_mgr.h"

namespace {

constexpr int ASIDE_W = 280;
constexpr int BODY_Y = HEADER_H + GAP;
constexpr int BODY_H = SCREEN_H - BODY_Y - PAD;
constexpr int LIST_W = SCREEN_W - 2 * PAD - ASIDE_W - GAP;

lv_obj_t *list, *qr, *urlLabel, *pinLabel;
String qrText;

void confirmRemove(uint8_t index) {
  static lv_obj_t *layer;
  static uint8_t idx;
  idx = index;
  const Theme &t = theme();
  layer = mk_box(lv_layer_top(), t.dark ? 0x07090B : t.bg, 0);
  lv_obj_set_size(layer, SCREEN_W, SCREEN_H);
  lv_obj_add_flag(layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *card = mk_box(layer, t.card, 24);
  lv_obj_set_width(card, 480);
  lv_obj_set_style_pad_all(card, 24, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 14, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);
  const String name = index < g_settings.accounts.size() ? g_settings.accounts[index].label : String("");
  mk_label(card, &font_20b, t.text, ("Remove " + name + "?").c_str());
  lv_obj_t *msg = mk_label(card, &font_14, t.muted,
                           "Its meetings and tasks leave the display. Nothing is deleted from Google.");
  lv_obj_set_width(msg, 432);
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
  lv_obj_t *row = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_width(row, 432);
  set_flex(row, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
  on_click(mk_text_button(row, "Cancel", false, 120), [](lv_event_t *) { lv_obj_del_async(layer); });
  lv_obj_t *rm = mk_text_button(row, "Remove", true, 120);
  lv_obj_set_style_bg_color(rm, c(theme().badBg), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(rm, 0), c(theme().badFg), 0);
  on_click(rm, [](lv_event_t *) {
    lv_obj_del_async(layer);
    Settings s = g_settings;
    if (idx < s.accounts.size()) s.accounts.erase(s.accounts.begin() + idx);
    ui_save_settings(s);
    lv_async_call([](void *) { accounts_refresh(); }, nullptr);
  });
}

}  // namespace

lv_obj_t *accounts_create() {
  const Theme &t = theme();
  lv_obj_t *scr = mk_screen();
  lv_obj_t *h = mk_back_header(scr, "Google accounts", Screen::Settings);
  lv_obj_t *sync = mk_button(h, t.card, TOUCH / 2);
  lv_obj_set_size(sync, LV_SIZE_CONTENT, TOUCH);
  lv_obj_set_style_pad_hor(sync, 16, 0);
  set_flex(sync, LV_FLEX_FLOW_ROW, 8);
  mk_label(sync, &font_14, t.text, ICON_REFRESH);
  mk_label(sync, &font_14b, t.text, "Sync all now");
  on_click(sync, [](lv_event_t *) {
    sync_refresh();
    toast("Syncing...");
  });

  list = mk_scroll_column(scr, 10);
  lv_obj_set_pos(list, PAD, BODY_Y);
  lv_obj_set_size(list, LIST_W, BODY_H);

  lv_obj_t *aside = mk_box(scr, t.panel, 16);
  lv_obj_set_pos(aside, SCREEN_W - PAD - ASIDE_W, BODY_Y);
  lv_obj_set_size(aside, ASIDE_W, BODY_H);
  lv_obj_set_style_pad_all(aside, 16, 0);
  set_flex(aside, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  mk_label(aside, &font_16s, t.text, "Add an account");
  lv_obj_t *qrBox = mk_box(aside, 0xFFFFFF, 12);
  lv_obj_set_size(qrBox, 132, 132);
  lv_obj_set_style_align(qrBox, LV_ALIGN_TOP_MID, 0);
  lv_obj_set_style_pad_all(qrBox, 8, 0);
  qr = lv_qrcode_create(qrBox, 116, lv_color_black(), lv_color_white());
  lv_obj_center(qr);
  qrText = "";
  urlLabel = mk_label(aside, &font_14b, t.text, "");
  lv_obj_t *steps = mk_label(aside, &font_12, t.muted,
                             "1. Scan the code, or open the address above on a phone or PC on this Wi-Fi.\n"
                             "2. Follow the steps to set up the Google script in the new account.\n"
                             "3. Paste its link with this PIN:");
  lv_obj_set_width(steps, ASIDE_W - 32);
  lv_label_set_long_mode(steps, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(steps, 3, 0);
  pinLabel = mk_label(aside, &font_26b, t.accent, "");
  lv_obj_set_style_text_letter_space(pinLabel, 4, 0);
  return scr;
}

void accounts_refresh() {
  const Theme &t = theme();
  // The QR code points at the IP address: ".local" names don't work on every phone.
  const String ip = wifi_ip();
  const String url = ip.length() ? "http://" + ip : web_url();
  if (url != qrText) {
    qrText = url;
    lv_qrcode_update(qr, url.c_str(), url.length());
  }
  lv_label_set_text(urlLabel, url.c_str());
  lv_label_set_text(pinLabel, web_pin().c_str());

  lv_obj_clean(list);
  for (uint8_t i = 0; i < g_settings.accounts.size(); i++) {
    const Account &a = g_settings.accounts[i];
    const AccountStatus st = i < g_snap.accounts.size() ? g_snap.accounts[i] : AccountStatus();
    lv_obj_t *card = mk_box(list, t.card, 16);
    lv_obj_set_width(card, LIST_W);
    lv_obj_set_style_pad_left(card, 16, 0);
    lv_obj_set_style_pad_right(card, 12, 0);
    lv_obj_set_style_pad_ver(card, 12, 0);
    set_flex(card, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *top = mk_box(card, 0, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_width(top, LIST_W - 28);
    set_flex(top, LV_FLEX_FLOW_ROW, 12);
    lv_obj_t *avatar = mk_box(top, a.color, 20);
    lv_obj_set_size(avatar, 40, 40);
    lv_obj_center(mk_label(avatar, &font_16s, 0x101317, initial_of(a.label).c_str()));
    lv_obj_t *names = mk_box(top, 0, 0);
    lv_obj_set_style_bg_opa(names, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(names, 1);
    set_flex(names, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    mk_label(names, &font_16s, t.text, a.label.c_str());
    lv_obj_t *email = mk_label(names, &font_14, t.muted, st.email.length() ? st.email.c_str() : "Not synced yet");
    lv_label_set_long_mode(email, LV_LABEL_LONG_DOT);
    lv_obj_set_width(email, LIST_W - 240);

    const bool bad = st.error.length() > 0;
    String chipText = bad ? String("Problem") : st.syncedAt ? "Synced " + fmt_time(st.syncedAt) : String("Waiting");
    lv_obj_t *chip = mk_box(top, bad ? t.badBg : t.raised, 12);
    lv_obj_set_style_pad_hor(chip, 10, 0);
    lv_obj_set_style_pad_ver(chip, 4, 0);
    mk_label(chip, &font_12, bad ? t.badFg : st.syncedAt ? t.good : t.muted, chipText.c_str());

    lv_obj_t *bottom = mk_box(card, 0, 0);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_TRANSP, 0);
    lv_obj_set_width(bottom, LIST_W - 28);
    set_flex(bottom, LV_FLEX_FLOW_ROW, 8);
    int shown = 0;
    for (const CalendarInfo &cal : st.calendars) shown += cal.shown;
    String summary = bad ? st.error : String(shown) + " of " + String((int)st.calendars.size()) + " calendars shown";
    lv_obj_t *sl = mk_label(bottom, &font_12, bad ? t.badFg : t.muted, summary.c_str());
    lv_label_set_long_mode(sl, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(sl, 1);
    lv_obj_t *choose = mk_text_button(bottom, "Choose calendars", false);
    on_click(choose, [](lv_event_t *e) {
      calendars_dialog_show((uint8_t)(intptr_t)lv_event_get_user_data(e));
    }, (void *)(intptr_t)i);
    lv_obj_t *rm = mk_icon_button(bottom, ICON_TRASH, t.muted);
    on_click(rm, [](lv_event_t *e) { confirmRemove((uint8_t)(intptr_t)lv_event_get_user_data(e)); },
             (void *)(intptr_t)i);
  }
  if (g_settings.accounts.empty()) {
    lv_obj_t *empty = mk_box(list, t.card, 16);
    lv_obj_set_width(empty, LIST_W);
    lv_obj_set_style_pad_all(empty, 18, 0);
    lv_obj_t *l = mk_label(empty, &font_16, t.muted,
                           "No accounts yet. Use the steps on the right to add your first Google account.");
    lv_obj_set_width(l, LIST_W - 36);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  }
}
