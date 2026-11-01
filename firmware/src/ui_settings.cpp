// Settings (round 7, design C "Dashboard tiles"): status tiles for Wi-Fi, accounts and
// the device, the two sliders, a row of toggle chips, and a row of small controls.
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
constexpr const char *kWifiNotSetUp = "Not set up";
constexpr const char *kStrong = "Strong";
constexpr const char *kGood = "Good";
constexpr const char *kWeak = "Weak";
constexpr int kStrongRssi = -60;  // dBm
constexpr int kGoodRssi = -72;
constexpr const char *kSignalAndIp = "%s \xC2\xB7 %s";
constexpr const char *kConnecting = "Connecting...";
constexpr const char *kNotConnected = "Not connected";

constexpr const char *kCardAccounts = "ACCOUNTS";
constexpr const char *kConnectedCount = "%d connected";
constexpr const char *kNeedsAttention = "%d needs attention";
constexpr const char *kLastSynced = "Synced %s";
constexpr const char *kSavedCopy = "Showing saved copy";
constexpr const char *kNotSynced = "Not synced yet";
constexpr const char *kSyncing = "Syncing...";

constexpr const char *kCardDevice = "DEVICE";
constexpr const char *kBattery = "%s %d%%%s";  // icon, percent, charging bolt
constexpr const char *kSdFree = "%.1f GB";
constexpr const char *kNoSd = "No SD card";
constexpr const char *kJoin = "  \xC2\xB7  ";
constexpr const char *kFirmware = "fw %s \xC2\xB7";
constexpr const char *kUpdate = "Update";

constexpr const char *kBrightness = "Brightness";
constexpr int kMinBrightness = 10;  // percent; lower is hard to read
constexpr const char *kVolume = "Volume";

constexpr const char *kNightMode = "Night mode";
constexpr const char *kNightModeSub = "%02d:%02d - %02d:%02d";
constexpr const char *kChime = "Chime";
constexpr const char *kChimeSub = "%d min before";
constexpr const char *kDim = "Dim idle";
constexpr const char *kDimSub = "after %d min";
constexpr const char *kAutoUpdate = "Auto-update";
constexpr const char *kAutoUpdateSub = "when idle";

constexpr const char *kSync = "Sync";
constexpr const char *kInterval = "%d min";
constexpr const char *kTime = "Time";
constexpr const char *kFormats[] = {"24 h", "12 h"};
constexpr const char *kTheme = "Theme";
constexpr const char *kFeatures = "Features";
constexpr const char *kFeaturesOn = "%d on";

constexpr int BODY_Y = HEADER_H + GAP;
constexpr int BODY_W = SCREEN_W - 2 * PAD;
constexpr int ROW_GAP = 12;
constexpr int TILE_H = 102;
constexpr int CHIP_H = 60;
constexpr int CELL_H = 64;
constexpr int SYNC_W = 290, TIME_W = 196, THEME_W = 140;  // Features gets the rest

// ---- State -------------------------------------------------------------------

lv_obj_t *wifiName, *wifiInfo;
lv_obj_t *accCount, *accInfo;
lv_obj_t *devInfo;
lv_obj_t *swNight, *swChime, *swDim, *swAuto, *themeName, *featuresOn;

// ---- Code --------------------------------------------------------------------

lv_obj_t *row(lv_obj_t *parent, int h) {
  lv_obj_t *r = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
  lv_obj_set_size(r, BODY_W, h);
  set_flex(r, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  return r;
}

// A card in a row: w wide, or an equal share of what is left when w is 0.
lv_obj_t *cell(lv_obj_t *parent, bool clickable, int w = 0) {
  const Theme &t = theme();
  lv_obj_t *cd = clickable ? mk_button(parent, t.card, 14) : mk_box(parent, t.card, 14);
  lv_obj_set_height(cd, lv_pct(100));
  lv_obj_set_width(cd, w);
  if (!w) lv_obj_set_flex_grow(cd, 1);
  lv_obj_set_style_pad_hor(cd, 12, 0);
  set_flex(cd, LV_FLEX_FLOW_ROW, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  return cd;
}

lv_obj_t *spacer(lv_obj_t *parent) {
  lv_obj_t *s = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
  lv_obj_set_size(s, 0, 1);
  lv_obj_set_flex_grow(s, 1);
  return s;
}

lv_obj_t *line(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = mk_label(parent, font, color, "");
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_size(l, lv_pct(100), lv_font_get_line_height(font));  // one line: long names end in "..."
  return l;
}

// Status tile: caption, a big line and a small line; tapping opens more.
lv_obj_t *tile(lv_obj_t *parent, const char *caption, lv_obj_t **big, lv_event_cb_t cb) {
  const Theme &t = theme();
  lv_obj_t *cd = cell(parent, true);
  lv_obj_set_style_pad_ver(cd, 12, 0);
  set_flex(cd, LV_FLEX_FLOW_COLUMN, 4, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *head = row(cd, 16);
  lv_obj_set_width(head, lv_pct(100));
  lv_obj_t *cap = mk_label(head, &font_12, t.muted, caption);
  lv_obj_set_style_text_letter_space(cap, 1, 0);
  spacer(head);
  mk_label(head, &font_14, t.muted, ICON_RIGHT);
  *big = line(cd, &font_20b, t.text);
  on_click(cd, cb);
  return cd;
}

// Title with a small line under it; returns the small line.
lv_obj_t *titled(lv_obj_t *parent, const char *title, const char *sub) {
  const Theme &t = theme();
  lv_obj_t *col = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_grow(col, 1);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 0, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  mk_label(col, &font_14b, t.text, title);
  return mk_label(col, &font_12, t.muted, sub);
}

// Toggle chip: title, a small line and a switch.
lv_obj_t *chip(lv_obj_t *parent, const char *title, const String &sub, bool on) {
  lv_obj_t *cd = cell(parent, false);
  titled(cd, title, sub.c_str());
  return mk_switch(cd, on, [](lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    Settings s = g_settings;
    if (sw == swNight) s.nightMode = on;
    if (sw == swChime) s.chime = on;
    if (sw == swDim) s.dimIdle = on;
    if (sw == swAuto) s.autoUpdate = on;
    ui_save_settings(s);
  });
}

// Sync interval: -/+ over the choices in kSyncChoices (the stepper's value is the index).
constexpr int kSyncCount = sizeof(kSyncChoices) / sizeof(kSyncChoices[0]);

int intervalIndex() {
  int i = 0;
  while (i < kSyncCount - 1 && kSyncChoices[i] < g_settings.refreshMinutes) i++;
  return i;
}

String intervalText(int i) { return strf(kInterval, (int)kSyncChoices[i]); }

void intervalStepped(int i, void *) {
  Settings s = g_settings;
  s.refreshMinutes = kSyncChoices[i];
  ui_save_settings(s);
}

}  // namespace

lv_obj_t *settings_create() {
  const Theme &t = theme();
  lv_obj_t *scr = mk_screen();
  mk_nav_header(scr, Screen::Settings);
  lv_obj_t *body = mk_box(scr, 0, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(body, PAD, BODY_Y);
  lv_obj_set_size(body, BODY_W, SCREEN_H - BODY_Y - PAD);
  set_flex(body, LV_FLEX_FLOW_COLUMN, ROW_GAP, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *tiles = row(body, TILE_H);
  lv_obj_t *wifi = tile(tiles, kCardWifi, &wifiName, [](lv_event_t *) { ui_show(Screen::Wifi); });
  wifiInfo = line(wifi, &font_14, t.muted);
  lv_obj_t *acc = tile(tiles, kCardAccounts, &accCount, [](lv_event_t *) { ui_show(Screen::Accounts); });
  accInfo = line(acc, &font_14, t.muted);
  lv_obj_t *dev = tile(tiles, kCardDevice, &devInfo, [](lv_event_t *) { ota_dialog_show(); });
  lv_obj_t *fw = row(dev, 18);
  lv_obj_set_width(fw, lv_pct(100));
  lv_obj_set_style_pad_column(fw, 6, 0);
  mk_label(fw, &font_14, t.muted, strf(kFirmware, FIRMWARE_VERSION).c_str());
  mk_label(fw, &font_14b, t.accent, kUpdate);

  lv_obj_t *sliders = mk_box(body, t.card, 14);
  lv_obj_set_size(sliders, BODY_W, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(sliders, 16, 0);
  lv_obj_set_style_pad_ver(sliders, 10, 0);
  set_flex(sliders, LV_FLEX_FLOW_COLUMN, 4, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *bright = mk_slider(sliders, kBrightness, g_settings.brightness, kMinBrightness, 100);
  lv_obj_add_event_cb(bright, [](lv_event_t *e) {
    ui_set_backlight(lv_slider_get_value(lv_event_get_target(e)));
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(bright, [](lv_event_t *e) {
    Settings s = g_settings;
    s.brightness = lv_slider_get_value(lv_event_get_target(e));
    ui_save_settings(s);
  }, LV_EVENT_RELEASED, nullptr);
  lv_obj_t *vol = mk_slider(sliders, kVolume, g_settings.volume, 0, 100);
  lv_obj_add_event_cb(vol, [](lv_event_t *e) {
    Settings s = g_settings;
    s.volume = lv_slider_get_value(lv_event_get_target(e));
    ui_save_settings(s);
    audio_chime(s.volume);  // preview
  }, LV_EVENT_RELEASED, nullptr);

  const Settings &g = g_settings;
  lv_obj_t *chips = row(body, CHIP_H);
  swNight = chip(chips, kNightMode, strf(kNightModeSub, g.nightStart / 60, g.nightStart % 60, g.nightEnd / 60, g.nightEnd % 60), g.nightMode);
  swChime = chip(chips, kChime, strf(kChimeSub, (int)g.remindMinutes), g.chime);
  swDim = chip(chips, kDim, strf(kDimSub, (int)g.dimAfterMinutes), g.dimIdle);
  swAuto = chip(chips, kAutoUpdate, kAutoUpdateSub, g.autoUpdate);

  lv_obj_t *cells = row(body, CELL_H);
  lv_obj_t *sync = cell(cells, false, SYNC_W);
  mk_label(sync, &font_14b, t.text, kSync);
  spacer(sync);
  mk_stepper(sync, intervalIndex(), 0, kSyncCount - 1, 1, intervalText, intervalStepped, nullptr, 56);
  on_click(mk_icon_button(sync, ICON_REFRESH, t.accent), [](lv_event_t *) {
    sync_refresh();
    toast(kSyncing);
  });

  lv_obj_t *time = cell(cells, false, TIME_W);
  mk_label(time, &font_14b, t.text, kTime);
  spacer(time);
  mk_segmented(time, kFormats, 2, g.use24h ? 0 : 1, 52, [](int i, void *) {
    Settings s = g_settings;
    s.use24h = i == 0;
    ui_save_settings(s);
    settings_refresh();  // the "Synced 12:43" time
  });

  lv_obj_t *th = cell(cells, true, THEME_W);
  themeName = titled(th, kTheme, "");
  mk_label(th, &font_14, t.accent, ICON_PALETTE);
  on_click(th, [](lv_event_t *) { theme_picker_show(); });

  lv_obj_t *feat = cell(cells, true);
  featuresOn = titled(feat, kFeatures, "");
  mk_label(feat, &font_14, t.accent, ICON_RIGHT);
  on_click(feat, [](lv_event_t *) { ui_show(Screen::Features); });
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

  // Accounts: a problem first, then the sync error, then when the last sync was.
  const size_t n = g_settings.accounts.size(), nIcs = g_settings.ics.size();
  lv_label_set_text(accCount, strf(kConnectedCount, (int)(n + nIcs)).c_str());
  int problems = 0;
  for (size_t i = 0; i < n && i < g_snap.accounts.size(); i++) problems += g_snap.accounts[i].error.length() > 0;
  for (size_t i = 0; i < nIcs && i < g_snap.ics.size(); i++) problems += g_snap.ics[i].error.length() > 0;
  String info = problems ? strf(kNeedsAttention, problems) : g_snap.error;
  uint32_t color = t.badFg;
  if (!info.length()) {
    color = g_snap.syncedAt ? t.good : t.muted;
    info = g_snap.syncedAt ? strf(kLastSynced, fmt_time(g_snap.syncedAt).c_str())
                           : String(g_snap.offline ? kSavedCopy : kNotSynced);
  }
  lv_label_set_text(accInfo, info.c_str());
  lv_obj_set_style_text_color(accInfo, c(color), 0);

  String dev;
  const int pct = battery_percent();
  if (pct >= 0) {
    const char *icon = pct > 80 ? ICON_BAT_FULL : pct > 55 ? ICON_BAT_3 : pct > 30 ? ICON_BAT_2 : pct > 10 ? ICON_BAT_1 : ICON_BAT_EMPTY;
    dev = strf(kBattery, icon, pct, battery_charging() ? ICON_BOLT : "") + kJoin;
  }
  dev += storage_ok() ? strf(kSdFree, storage_free_bytes() / 1e9) : String(kNoSd);
  lv_label_set_text(devInfo, dev.c_str());

  auto setSwitch = [](lv_obj_t *sw, bool on) {
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    else lv_obj_clear_state(sw, LV_STATE_CHECKED);
  };
  setSwitch(swNight, g_settings.nightMode);
  setSwitch(swAuto, g_settings.autoUpdate);
  setSwitch(swChime, g_settings.chime);
  setSwitch(swDim, g_settings.dimIdle);
  lv_label_set_text(themeName, theme().name);
  lv_label_set_text(featuresOn, strf(kFeaturesOn, features_on_count()).c_str());
}
