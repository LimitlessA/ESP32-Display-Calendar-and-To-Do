// Full-screen overlays on the top layer: meeting reminder (mockup 5),
// firmware update, theme picker and calendar chooser. They cover the whole
// screen with an opaque background, which is cheaper to draw than a dimmed one.
#include "config.h"
#include "ota.h"
#include "sync.h"
#include "ui_internal.h"
#include "web.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kStartingNow = "STARTING NOW";
constexpr const char *kStartsInMinute = "STARTS IN %d MINUTE";
constexpr const char *kStartsInMinutes = "STARTS IN %d MINUTES";
constexpr const char *kSnooze = "Snooze %d min";
constexpr int kSnoozeMinutes = 5;
constexpr const char *kDetails = "Details";
constexpr const char *kGotIt = "Got it";

constexpr const char *kOtaTitle = "Firmware update";
constexpr const char *kOtaChecking = "Checking for updates...";
constexpr const char *kOtaUpToDate = "You have the latest version (%s).";
constexpr const char *kOtaAvailable = "Version %s is available. You have %s. The display restarts when it is done.";
constexpr const char *kOtaInstalling = "Installing... %d%%  Keep the display powered.";
constexpr const char *kOtaDone = "Installed. Restarting...";
constexpr const char *kOtaFailed = "%s\n\nYou can also upload a firmware file at %s (PIN %s).";  // error, address, PIN
constexpr const char *kClose = "Close";
constexpr const char *kInstall = "Install";

constexpr const char *kThemeTitle = "Theme";
constexpr const char *kDone = "Done";
constexpr const char *kCalendarsTitle = "Calendars: %s";  // account name
constexpr const char *kSyncFirst = "Sync this account first.";

// ---- State -------------------------------------------------------------------

lv_obj_t *reminderLayer = nullptr;
Event reminderEvent;

lv_obj_t *otaLayer = nullptr;
lv_obj_t *otaTitle, *otaText, *otaBar, *otaButtons;
OtaState otaShownState = OtaState::Idle;
int otaShownProgress = -1;

lv_obj_t *themeLayer = nullptr;
lv_obj_t *calLayer = nullptr;
uint8_t calAccount;  // account whose calendars are being chosen

// ---- Code --------------------------------------------------------------------

lv_obj_t *mk_layer() {
  lv_obj_t *layer = mk_box(lv_layer_top(), theme().dark ? 0x07090B : theme().bg, 0);
  lv_obj_set_size(layer, SCREEN_W, SCREEN_H);
  lv_obj_add_flag(layer, LV_OBJ_FLAG_CLICKABLE);  // swallow taps behind the dialog
  return layer;
}

lv_obj_t *mk_dialog(lv_obj_t *layer, int w) {
  lv_obj_t *card = mk_box(layer, theme().card, 24);
  lv_obj_set_width(card, w);
  lv_obj_set_style_pad_hor(card, 28, 0);
  lv_obj_set_style_pad_ver(card, 24, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 14, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);
  return card;
}

void closeLayer(lv_obj_t **layer) {
  if (*layer) lv_obj_del_async(*layer);
  *layer = nullptr;
}

}  // namespace

// ---- Meeting reminder --------------------------------------------------------

void reminder_show(const Event &ev) {
  reminder_hide();
  reminderEvent = ev;
  const Theme &t = theme();
  reminderLayer = mk_layer();
  lv_obj_t *card = mk_dialog(reminderLayer, 560);
  lv_obj_set_style_border_color(card, c(t.accent), 0);
  lv_obj_set_style_border_width(card, 2, 0);

  lv_obj_t *top = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
  set_flex(top, LV_FLEX_FLOW_ROW, 14);
  lv_obj_t *bell = mk_box(top, t.warnBg, 26);
  lv_obj_set_size(bell, 52, 52);
  lv_obj_center(mk_label(bell, &font_20b, t.accent, ICON_BELL));
  lv_obj_t *col = mk_box(top, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  const long mins = (long)((ev.start / 1000 - ui_now() + 30) / 60);
  String when = mins <= 0 ? String(kStartingNow) : strf(mins == 1 ? kStartsInMinute : kStartsInMinutes, (int)mins);
  lv_obj_t *w = mk_label(col, &font_14b, t.accent, when.c_str());
  lv_obj_set_style_text_letter_space(w, 1, 0);
  mk_label(col, &font_14, t.muted, ev.calendar.c_str());

  lv_obj_t *title = mk_label(card, &font_34b, t.text, ev.title.c_str());
  lv_obj_set_width(title, 500);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  String meta = fmt_time(ev.start / 1000) + " - " + fmt_time(ev.end / 1000);
  if (ev.location.length()) meta += "    " + ev.location;
  lv_obj_t *m = mk_label(card, &font_16, t.muted, meta.c_str());
  lv_label_set_long_mode(m, LV_LABEL_LONG_DOT);
  lv_obj_set_width(m, 500);

  lv_obj_t *row = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_width(row, 504);
  lv_obj_set_style_pad_top(row, 6, 0);
  set_flex(row, LV_FLEX_FLOW_ROW, 10);
  lv_obj_t *snooze = mk_text_button(row, strf(kSnooze, kSnoozeMinutes).c_str(), false, 160);
  lv_obj_set_height(snooze, 56);
  on_click(snooze, [](lv_event_t *) {
    ui_snooze_reminder(reminderEvent.key, kSnoozeMinutes);
    reminder_hide();
  });
  lv_obj_t *details = mk_text_button(row, kDetails, false, 160);
  lv_obj_set_height(details, 56);
  on_click(details, [](lv_event_t *) {
    reminder_hide();
    ui_open_event(reminderEvent);
  });
  lv_obj_t *ok = mk_text_button(row, kGotIt, true, 164);
  lv_obj_set_height(ok, 56);
  on_click(ok, [](lv_event_t *) { reminder_hide(); });
}

void reminder_hide() {
  closeLayer(&reminderLayer);
}

bool reminder_visible() {
  return reminderLayer != nullptr;
}

// ---- Firmware update -----------------------------------------------------------

void ota_dialog_show() {
  if (otaLayer) return;
  const Theme &t = theme();
  otaLayer = mk_layer();
  lv_obj_t *card = mk_dialog(otaLayer, 520);
  otaTitle = mk_label(card, &font_20b, t.text, kOtaTitle);
  otaText = mk_label(card, &font_16, t.muted, "");
  lv_obj_set_width(otaText, 464);
  lv_label_set_long_mode(otaText, LV_LABEL_LONG_WRAP);
  otaBar = lv_bar_create(card);
  lv_obj_set_size(otaBar, 464, 12);
  lv_obj_set_style_bg_color(otaBar, c(t.raised), LV_PART_MAIN);
  lv_obj_set_style_bg_color(otaBar, c(t.accent), LV_PART_INDICATOR);
  otaButtons = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(otaButtons, LV_OPA_TRANSP, 0);
  lv_obj_set_width(otaButtons, 464);
  set_flex(otaButtons, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
  otaShownState = OtaState::Idle;
  otaShownProgress = -1;
  if (ota_status().state != OtaState::Installing) ota_check();
  ota_dialog_tick();
}

void ota_dialog_tick() {
  const OtaStatus st = ota_status();
  // An upload from the setup page should show progress even if nobody opened the dialog.
  if (!otaLayer && st.state == OtaState::Installing) ota_dialog_show();
  if (!otaLayer) return;
  if (st.state == otaShownState && st.progress == otaShownProgress) return;
  otaShownState = st.state;
  otaShownProgress = st.progress;

  String text;
  bool bar = false, closable = true, install = false;
  switch (st.state) {
    case OtaState::Idle:
    case OtaState::Checking: text = kOtaChecking; break;
    case OtaState::UpToDate: text = strf(kOtaUpToDate, FIRMWARE_VERSION); break;
    case OtaState::Available:
      text = strf(kOtaAvailable, st.latest.c_str(), FIRMWARE_VERSION);
      install = true;
      break;
    case OtaState::Installing:
      text = strf(kOtaInstalling, st.progress);
      bar = true;
      closable = false;
      break;
    case OtaState::Done: text = kOtaDone; bar = true; closable = false; break;
    case OtaState::Failed:
      text = strf(kOtaFailed, st.message.c_str(), web_url().c_str(), web_pin().c_str());
      break;
  }
  lv_label_set_text(otaText, text.c_str());
  if (bar) {
    lv_obj_clear_flag(otaBar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(otaBar, st.progress, LV_ANIM_OFF);
  } else {
    lv_obj_add_flag(otaBar, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_clean(otaButtons);
  if (closable) {
    on_click(mk_text_button(otaButtons, kClose, false, 120), [](lv_event_t *) { closeLayer(&otaLayer); });
  }
  if (install) {
    on_click(mk_text_button(otaButtons, kInstall, true, 140), [](lv_event_t *) { ota_install(); });
  }
}

// ---- Theme picker ----------------------------------------------------------------

void theme_picker_show() {
  const Theme &t = theme();
  themeLayer = mk_layer();
  lv_obj_t *card = mk_dialog(themeLayer, 720);
  lv_obj_t *head = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_width(head, 664);
  set_flex(head, LV_FLEX_FLOW_ROW, 0, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
  mk_label(head, &font_20b, t.text, kThemeTitle);
  on_click(mk_text_button(head, kDone, false, 100), [](lv_event_t *) { closeLayer(&themeLayer); });

  lv_obj_t *grid = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_width(grid, 664);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, 12, 0);
  lv_obj_set_style_pad_column(grid, 14, 0);
  for (uint8_t i = 0; i < kThemeCount; i++) {
    const Theme &th = kThemes[i];
    const bool on = i == g_settings.theme;
    // A miniature of the Home screen in that theme.
    lv_obj_t *tile = mk_button(grid, th.bg, 14);
    lv_obj_set_size(tile, 212, 132);
    lv_obj_set_style_bg_color(tile, c(th.bg), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(tile, on ? 3 : 1, 0);
    lv_obj_set_style_border_color(tile, c(on ? t.accent : t.line), 0);
    lv_obj_set_style_pad_all(tile, 10, 0);
    lv_obj_t *bar = mk_box(tile, th.header, 4);
    lv_obj_set_size(bar, 188, 14);
    for (int k = 0; k < 3; k++) {
      lv_obj_t *col = mk_box(tile, k == 1 ? th.panel : th.card, 6);
      lv_obj_set_pos(col, k * 64, 22);
      lv_obj_set_size(col, 58, 54);
      lv_obj_t *line = mk_box(col, k == 0 ? th.accent : th.muted, 2);
      lv_obj_set_pos(line, 6, 8);
      lv_obj_set_size(line, k == 0 ? 26 : 40, 5);
      lv_obj_t *line2 = mk_box(col, th.text, 2);
      lv_obj_set_pos(line2, 6, 20);
      lv_obj_set_size(line2, 44, 5);
    }
    lv_obj_t *name = mk_label(tile, &font_14b, th.text, th.name);
    lv_obj_align(name, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    if (on) {
      lv_obj_t *check = mk_label(tile, &font_14, th.accent, ICON_CHECK);
      lv_obj_align(check, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }
    on_click(tile, [](lv_event_t *e) {
      const uint8_t idx = (uint8_t)(intptr_t)lv_event_get_user_data(e);
      lv_async_call([](void *p) {
        closeLayer(&themeLayer);
        Settings s = g_settings;
        s.theme = (uint8_t)(intptr_t)p;
        ui_save_settings(s);  // rebuilds every screen in the new colors
        theme_picker_show();
      }, (void *)(intptr_t)idx);
    }, (void *)(intptr_t)i);
  }
}

// ---- Calendar chooser ---------------------------------------------------------

void calendars_dialog_show(uint8_t account) {
  calAccount = account;
  const Theme &t = theme();
  calLayer = mk_layer();
  lv_obj_t *card = mk_dialog(calLayer, 560);
  lv_obj_set_height(card, SCREEN_H - 60);
  lv_obj_t *head = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_width(head, 504);
  set_flex(head, LV_FLEX_FLOW_ROW, 0, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
  const String name = account < g_settings.accounts.size() ? g_settings.accounts[account].label : String("");
  mk_label(head, &font_20b, t.text, strf(kCalendarsTitle, name.c_str()).c_str());
  on_click(mk_text_button(head, kDone, true, 100), [](lv_event_t *) {
    closeLayer(&calLayer);
    sync_refresh();
  });

  lv_obj_t *list = mk_scroll_column(card, 2);
  lv_obj_set_width(list, 504);
  lv_obj_set_flex_grow(list, 1);
  if (account >= g_snap.accounts.size() || g_snap.accounts[account].calendars.empty()) {
    mk_label(list, &font_16, t.muted, kSyncFirst);
    return;
  }
  for (const CalendarInfo &cal : g_snap.accounts[account].calendars) {
    lv_obj_t *row = mk_box(list, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_size(row, 496, 52);
    set_flex(row, LV_FLEX_FLOW_ROW, 12);
    mk_dot(row, cal.color, 12);
    lv_obj_t *l = mk_label(row, &font_16, t.text, cal.name.c_str());
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(l, 1);
    // The id lives on the row, which outlives its switch.
    mk_switch(row, cal.shown, [](lv_event_t *e) {
      const String *id = static_cast<String *>(lv_event_get_user_data(e));
      sync_show_calendar(calAccount, *id, lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    }, attach(row, cal.id));
  }
}
