// Settings (mockup 9): Wi-Fi, accounts, device, display and sound, sync.
#include "audio.h"
#include "config.h"
#include "battery.h"
#include "storage.h"
#include "sync.h"
#include "ui_internal.h"
#include "wifi_mgr.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kCardWifi = "WI-FI";
constexpr const char *kChangeNetwork = "Change network  " ICON_RIGHT;
constexpr const char *kWifiNotSetUp = "Not set up";
constexpr const char *kStrong = "Strong signal";
constexpr const char *kGood = "Good signal";
constexpr const char *kWeak = "Weak signal";
constexpr int kStrongRssi = -60;  // dBm
constexpr int kGoodRssi = -72;
constexpr const char *kSignalAndIp = "%s \xC2\xB7 %s";
constexpr const char *kConnecting = "Connecting...";
constexpr const char *kNotConnected = "Not connected";

constexpr const char *kCardAccounts = "ACCOUNTS";
constexpr const char *kManageAccounts = "Manage accounts  " ICON_RIGHT;
constexpr const char *kConnectedCount = "%d connected";
constexpr const char *kNeedsAttention = "%s needs attention";
constexpr const char *kAllSynced = "All synced";
constexpr const char *kAccountKinds = "%d Google \xC2\xB7 %d ICS \xC2\xB7 desk %s";
constexpr const char *kDeskOn = "on";
constexpr const char *kDeskOff = "off";

constexpr const char *kCardDevice = "DEVICE";
constexpr const char *kSdFree = ICON_SD "  SD card: %.1f GB free";
constexpr const char *kNoSd = ICON_SD "  No SD card";
constexpr const char *kBattery = "%s  Battery: %d%%%s";  // icon, percent, ", charging"
constexpr const char *kCharging = ", charging";
constexpr const char *kNoBattery = ICON_BAT_EMPTY "  No battery gauge";
constexpr const char *kFirmware = "Firmware %s";
constexpr const char *kCheckUpdate = "Check for update";

constexpr const char *kCardDisplay = "DISPLAY AND SOUND";
constexpr const char *kBrightness = "Brightness";
constexpr int kMinBrightness = 10;  // percent; lower is hard to read
constexpr const char *kVolume = "Reminder volume";
constexpr const char *kTheme = "Theme";
constexpr const char *kNightMode = "Night mode";
constexpr const char *kNightModeSub = "%02d:%02d - %02d:%02d, dim clock";
constexpr const char *kChime = "Meeting chime";
constexpr const char *kChimeSub = "%d min before each meeting";
constexpr const char *kDim = "Dim when idle";
constexpr const char *kDimSub = "after %d minutes";
constexpr const char *kTimeFormat = "Time format";
constexpr const char *kFeatures = "Features";
constexpr const char *kFeaturesOn = "%d on";
constexpr const char *kLayouts = "Layouts";
constexpr const char *k24h = "24 h";
constexpr const char *k12h = "12 h";

constexpr const char *kCardSync = "SYNC";
constexpr const char *kLastSynced = "Last synced %s";
constexpr const char *kNextSync = "Every %d min \xC2\xB7 next in %d min";
constexpr const char *kEvery = "Every %d min";
constexpr const char *kInterval = ICON_CLOCK "  %d min";
constexpr const char *kSavedCopy = "Showing saved copy";
constexpr const char *kNotSynced = "Not synced yet";
constexpr const char *kAutoUpdate = "Auto-update";
constexpr const char *kAutoUpdateSub = "new firmware, when idle";
constexpr const char *kSyncNow = "Sync now";
constexpr const char *kSyncing = "Syncing...";

constexpr int BODY_Y = HEADER_H + GAP;
constexpr int CARD_GAP = 11;
constexpr int CARD_W = (SCREEN_W - 2 * PAD - 2 * CARD_GAP) / 3;
constexpr int ROW1_H = 150;
constexpr int ROW2_Y = BODY_Y + ROW1_H + 10;
constexpr int ROW2_H = SCREEN_H - ROW2_Y - PAD;
constexpr int WIDE_W = 2 * CARD_W + CARD_GAP;

// ---- State -------------------------------------------------------------------

lv_obj_t *wifiName, *wifiInfo;
lv_obj_t *accCount, *accProblem, *accEvery;
lv_obj_t *sdInfo, *batInfo;
lv_obj_t *syncLast, *syncNext, *intervalLabel;
lv_obj_t *swNight, *swChime, *swDim, *swAuto, *fmt24, *fmt12, *themeName, *featuresOn;

// ---- Code --------------------------------------------------------------------

lv_obj_t *card(lv_obj_t *scr, int x, int y, int w, int h, const char *heading, bool clickable) {
  const Theme &t = theme();
  lv_obj_t *cd = clickable ? mk_button(scr, t.card, 16) : mk_box(scr, t.card, 16);
  lv_obj_set_pos(cd, x, y);
  lv_obj_set_size(cd, w, h);
  lv_obj_set_style_pad_hor(cd, 16, 0);
  lv_obj_set_style_pad_ver(cd, 14, 0);
  set_flex(cd, LV_FLEX_FLOW_COLUMN, 6, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *hd = mk_label(cd, &font_12, t.muted, heading);
  lv_obj_set_style_text_letter_space(hd, 1, 0);
  return cd;
}

lv_obj_t *line(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int w) {
  lv_obj_t *l = mk_label(parent, font, color, "");
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_size(l, w, lv_font_get_line_height(font));  // one line: long names end in "..."
  return l;
}

void link(lv_obj_t *parent, const char *text) {
  lv_obj_t *spacer = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_grow(spacer, 1);
  mk_label(parent, &font_14b, theme().accent, text);
}

// One labelled control row in the Display and sound card.
lv_obj_t *controlRow(lv_obj_t *parent, const char *title, const char *sub) {
  const Theme &t = theme();
  lv_obj_t *row = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_size(row, lv_pct(100), 46);
  set_flex(row, LV_FLEX_FLOW_ROW, 8, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *col = mk_box(row, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 0, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  mk_label(col, &font_14b, t.text, title);
  if (sub) mk_label(col, &font_12, t.muted, sub);
  return row;
}

lv_obj_t *slider(lv_obj_t *parent, const char *title, int value, int min) {
  const Theme &t = theme();
  lv_obj_t *wrap = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
  lv_obj_set_size(wrap, lv_pct(100), 46);
  set_flex(wrap, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  mk_label(wrap, &font_14b, t.text, title);
  lv_obj_t *s = lv_slider_create(wrap);
  lv_obj_set_width(s, lv_pct(94));
  lv_obj_set_height(s, 8);
  lv_obj_set_style_align(s, LV_ALIGN_CENTER, 0);
  lv_slider_set_range(s, min, 100);
  lv_slider_set_value(s, value, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s, c(t.raised), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s, c(t.accent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s, c(t.accent), LV_PART_KNOB);
  lv_obj_set_style_pad_all(s, 6, LV_PART_KNOB);
  lv_obj_set_ext_click_area(s, 16);
  return s;
}

void toggled(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  Settings s = g_settings;
  if (sw == swNight) s.nightMode = on;
  if (sw == swChime) s.chime = on;
  if (sw == swDim) s.dimIdle = on;
  if (sw == swAuto) s.autoUpdate = on;
  ui_save_settings(s);
}

// Steps through the sync intervals on each tap.
void nextInterval(lv_event_t *) {
  Settings s = g_settings;
  const size_t n = sizeof(kSyncChoices) / sizeof(kSyncChoices[0]);
  size_t i = 0;
  while (i < n && kSyncChoices[i] <= s.refreshMinutes) i++;
  s.refreshMinutes = kSyncChoices[i < n ? i : 0];
  ui_save_settings(s);
  settings_refresh();
}

void setFormat(bool use24h) {
  Settings s = g_settings;
  s.use24h = use24h;
  ui_save_settings(s);
  settings_refresh();
}

}  // namespace

lv_obj_t *settings_create() {
  const Theme &t = theme();
  lv_obj_t *scr = mk_screen();
  mk_nav_header(scr, Screen::Settings);
  const int inner = CARD_W - 32;

  lv_obj_t *wifi = card(scr, PAD, BODY_Y, CARD_W, ROW1_H, kCardWifi, true);
  wifiName = line(wifi, &font_20b, t.text, inner);
  wifiInfo = line(wifi, &font_14, t.muted, inner);
  link(wifi, kChangeNetwork);
  on_click(wifi, [](lv_event_t *) { ui_show(Screen::Wifi); });

  lv_obj_t *acc = card(scr, PAD + CARD_W + CARD_GAP, BODY_Y, CARD_W, ROW1_H, kCardAccounts, true);
  accCount = line(acc, &font_20b, t.text, inner);
  accProblem = line(acc, &font_14, t.badFg, inner);
  accEvery = line(acc, &font_14, t.muted, inner);
  link(acc, kManageAccounts);
  on_click(acc, [](lv_event_t *) { ui_show(Screen::Accounts); });

  lv_obj_t *dev = card(scr, PAD + 2 * (CARD_W + CARD_GAP), BODY_Y, CARD_W, ROW1_H, kCardDevice, false);
  lv_obj_set_style_pad_row(dev, 4, 0);
  sdInfo = line(dev, &font_12, t.muted, inner);
  batInfo = line(dev, &font_12, t.muted, inner);
  lv_obj_t *fw = line(dev, &font_12, t.muted, inner);
  lv_label_set_text(fw, strf(kFirmware, FIRMWARE_VERSION).c_str());
  lv_obj_t *devSpacer = mk_box(dev, 0, 0);
  lv_obj_set_style_bg_opa(devSpacer, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_grow(devSpacer, 1);
  lv_obj_t *upd = mk_text_button(dev, kCheckUpdate, false, inner);
  lv_obj_set_height(upd, 40);
  on_click(upd, [](lv_event_t *) { ota_dialog_show(); });

  lv_obj_t *ds = card(scr, PAD, ROW2_Y, WIDE_W, ROW2_H, kCardDisplay, false);
  lv_obj_set_style_pad_row(ds, 2, 0);
  lv_obj_t *grid = mk_box(ds, 0, 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_size(grid, WIDE_W - 32, ROW2_H - 46);
  set_flex(grid, LV_FLEX_FLOW_ROW, 24, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  const int half = (WIDE_W - 32 - 24) / 2;
  lv_obj_t *left = mk_box(grid, 0, 0);
  lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
  lv_obj_set_width(left, half);
  set_flex(left, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *right = mk_box(grid, 0, 0);
  lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
  lv_obj_set_width(right, half);
  set_flex(right, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *bright = slider(left, kBrightness, g_settings.brightness, kMinBrightness);
  lv_obj_add_event_cb(bright, [](lv_event_t *e) {
    ui_set_backlight(lv_slider_get_value(lv_event_get_target(e)));
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(bright, [](lv_event_t *e) {
    Settings s = g_settings;
    s.brightness = lv_slider_get_value(lv_event_get_target(e));
    ui_save_settings(s);
  }, LV_EVENT_RELEASED, nullptr);

  lv_obj_t *vol = slider(left, kVolume, g_settings.volume, 0);
  lv_obj_add_event_cb(vol, [](lv_event_t *e) {
    Settings s = g_settings;
    s.volume = lv_slider_get_value(lv_event_get_target(e));
    ui_save_settings(s);
    audio_chime(s.volume);  // preview
  }, LV_EVENT_RELEASED, nullptr);

  lv_obj_t *themeRow = controlRow(left, kTheme, nullptr);
  lv_obj_t *themeBtn = mk_button(themeRow, t.raised, 22);
  lv_obj_set_size(themeBtn, LV_SIZE_CONTENT, 40);
  lv_obj_set_style_pad_hor(themeBtn, 14, 0);
  set_flex(themeBtn, LV_FLEX_FLOW_ROW, 8);
  mk_label(themeBtn, &font_14, t.accent, ICON_PALETTE);
  themeName = mk_label(themeBtn, &font_14b, t.text, theme().name);
  on_click(themeBtn, [](lv_event_t *) { theme_picker_show(); });

  lv_obj_t *featRow = controlRow(left, kFeatures, "");
  featuresOn = lv_obj_get_child(lv_obj_get_child(featRow, 0), 1);
  lv_obj_t *buttons = mk_box(featRow, 0, 0);
  lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
  set_flex(buttons, LV_FLEX_FLOW_ROW, 6);
  lv_obj_t *layoutsBtn = mk_button(buttons, t.raised, 20);
  lv_obj_set_size(layoutsBtn, LV_SIZE_CONTENT, 40);
  lv_obj_set_style_pad_hor(layoutsBtn, 12, 0);
  set_flex(layoutsBtn, LV_FLEX_FLOW_ROW, 6);
  mk_label(layoutsBtn, &font_14, t.accent, ICON_LAYERS);
  mk_label(layoutsBtn, &font_14b, t.text, kLayouts);
  on_click(layoutsBtn, [](lv_event_t *) { layout_picker_show(); });
  lv_obj_t *featBtn = mk_button(buttons, t.raised, 20);
  lv_obj_set_size(featBtn, 44, 40);
  lv_obj_center(mk_label(featBtn, &font_14, t.text, ICON_RIGHT));
  on_click(featBtn, [](lv_event_t *) { ui_show(Screen::Features); });

  const String night = strf(kNightModeSub, g_settings.nightStart / 60, g_settings.nightStart % 60,
                            g_settings.nightEnd / 60, g_settings.nightEnd % 60);
  swNight = mk_switch(controlRow(right, kNightMode, night.c_str()), g_settings.nightMode, toggled);
  const String chime = strf(kChimeSub, (int)g_settings.remindMinutes);
  swChime = mk_switch(controlRow(right, kChime, chime.c_str()), g_settings.chime, toggled);
  const String dim = strf(kDimSub, (int)g_settings.dimAfterMinutes);
  swDim = mk_switch(controlRow(right, kDim, dim.c_str()), g_settings.dimIdle, toggled);

  lv_obj_t *fmtRow = controlRow(right, kTimeFormat, nullptr);
  lv_obj_t *seg = mk_box(fmtRow, t.bg, 22);
  lv_obj_set_style_pad_all(seg, 2, 0);
  set_flex(seg, LV_FLEX_FLOW_ROW, 0);
  fmt24 = mk_button(seg, t.raised, 20);
  lv_obj_set_size(fmt24, 64, 40);
  lv_obj_set_style_min_height(fmt24, 40, 0);
  lv_obj_center(mk_label(fmt24, &font_14b, t.text, k24h));
  on_click(fmt24, [](lv_event_t *) { setFormat(true); });
  fmt12 = mk_button(seg, t.raised, 20);
  lv_obj_set_size(fmt12, 64, 40);
  lv_obj_set_style_min_height(fmt12, 40, 0);
  lv_obj_center(mk_label(fmt12, &font_14b, t.text, k12h));
  on_click(fmt12, [](lv_event_t *) { setFormat(false); });

  lv_obj_t *sync = card(scr, PAD + WIDE_W + CARD_GAP, ROW2_Y, CARD_W, ROW2_H, kCardSync, false);
  syncLast = line(sync, &font_20b, t.text, inner);
  syncNext = mk_label(sync, &font_14, t.muted, "");
  lv_obj_set_width(syncNext, inner);
  lv_label_set_long_mode(syncNext, LV_LABEL_LONG_WRAP);
  lv_obj_t *spacer = mk_box(sync, 0, 0);
  lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_grow(spacer, 1);
  swAuto = mk_switch(controlRow(sync, kAutoUpdate, kAutoUpdateSub), g_settings.autoUpdate,
                     toggled);
  lv_obj_t *buttons2 = mk_box(sync, 0, 0);
  lv_obj_set_style_bg_opa(buttons2, LV_OPA_TRANSP, 0);
  lv_obj_set_width(buttons2, inner);
  set_flex(buttons2, LV_FLEX_FLOW_ROW, 8);
  lv_obj_t *every = mk_button(buttons2, t.raised, TOUCH / 2);
  lv_obj_set_size(every, 92, TOUCH);
  intervalLabel = mk_label(every, &font_14, t.text, "");  // font_14 has the clock icon
  lv_obj_center(intervalLabel);
  on_click(every, nextInterval);
  lv_obj_t *now = mk_text_button(buttons2, kSyncNow, true, inner - 92 - 8);
  lv_obj_set_style_pad_hor(now, 8, 0);
  on_click(now, [](lv_event_t *) {
    sync_refresh();
    toast(kSyncing);
  });
  return scr;
}

void settings_refresh() {
  const Theme &t = theme();

  const WifiState ws = wifi_state();
  const String ssid = wifi_ssid();
  lv_label_set_text(wifiName, ssid.length() ? ssid.c_str() : kWifiNotSetUp);
  if (ws == WifiState::Connected) {
    const int rssi = wifi_rssi();
    const char *q = rssi > kStrongRssi ? kStrong : rssi > kGoodRssi ? kGood : kWeak;
    lv_label_set_text(wifiInfo, strf(kSignalAndIp, q, wifi_ip().c_str()).c_str());
  } else {
    lv_label_set_text(wifiInfo, ws == WifiState::Connecting ? kConnecting : kNotConnected);
  }

  const size_t n = g_settings.accounts.size(), nIcs = g_settings.ics.size();
  lv_label_set_text(accCount, strf(kConnectedCount, (int)(n + nIcs)).c_str());
  String problem;
  for (size_t i = 0; i < n && i < g_snap.accounts.size(); i++) {
    if (g_snap.accounts[i].error.length()) {
      problem = strf(kNeedsAttention, g_settings.accounts[i].label.c_str());
      break;
    }
  }
  for (size_t i = 0; !problem.length() && i < nIcs && i < g_snap.ics.size(); i++) {
    if (g_snap.ics[i].error.length()) problem = strf(kNeedsAttention, g_settings.ics[i].label.c_str());
  }
  lv_label_set_text(accProblem, problem.c_str());
  lv_obj_set_style_text_color(accProblem, c(problem.length() ? t.badFg : t.good), 0);
  if (!problem.length() && n) lv_label_set_text(accProblem, kAllSynced);
  lv_label_set_text(accEvery, strf(kAccountKinds, (int)n, (int)nIcs, g_settings.desk ? kDeskOn : kDeskOff).c_str());
  lv_label_set_text(intervalLabel, strf(kInterval, (int)g_settings.refreshMinutes).c_str());

  if (storage_ok()) {
    const double gb = storage_free_bytes() / 1e9;
    lv_label_set_text(sdInfo, strf(kSdFree, gb).c_str());
  } else {
    lv_label_set_text(sdInfo, kNoSd);
  }
  const int pct = battery_percent();
  if (pct >= 0) {
    const char *icon = pct > 80 ? ICON_BAT_FULL : pct > 55 ? ICON_BAT_3 : pct > 30 ? ICON_BAT_2 : pct > 10 ? ICON_BAT_1 : ICON_BAT_EMPTY;
    lv_label_set_text(batInfo, strf(kBattery, icon, pct, battery_charging() ? kCharging : "").c_str());
  } else {
    lv_label_set_text(batInfo, kNoBattery);
  }

  if (g_snap.syncedAt) {
    lv_label_set_text(syncLast, strf(kLastSynced, fmt_time(g_snap.syncedAt).c_str()).c_str());
    const long left = (long)(g_snap.syncedAt + g_settings.refreshMinutes * 60 - ui_now()) / 60;
    lv_label_set_text(syncNext, strf(kNextSync, (int)g_settings.refreshMinutes, (int)(left > 0 ? left : 0)).c_str());
  } else {
    lv_label_set_text(syncLast, g_snap.offline ? kSavedCopy : kNotSynced);
    lv_label_set_text(syncNext, strf(kEvery, (int)g_settings.refreshMinutes).c_str());
  }
  if (g_snap.error.length()) {
    lv_label_set_text(syncNext, g_snap.error.c_str());
    lv_obj_set_style_text_color(syncNext, c(t.badFg), 0);
  } else {
    lv_obj_set_style_text_color(syncNext, c(t.muted), 0);
  }

  auto setSwitch = [](lv_obj_t *sw, bool on) {
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    else lv_obj_clear_state(sw, LV_STATE_CHECKED);
  };
  setSwitch(swNight, g_settings.nightMode);
  setSwitch(swAuto, g_settings.autoUpdate);
  setSwitch(swChime, g_settings.chime);
  setSwitch(swDim, g_settings.dimIdle);
  lv_obj_set_style_bg_opa(fmt24, g_settings.use24h ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(fmt12, g_settings.use24h ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_label_set_text(themeName, theme().name);
  lv_label_set_text(featuresOn, strf(kFeaturesOn, features_on_count()).c_str());
}
