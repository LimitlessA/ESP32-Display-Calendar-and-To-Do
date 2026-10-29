// Wi-Fi: scan, pick a network, type the password on screen (mockup 7).
#include <vector>

#include "ui_internal.h"
#include "wifi_mgr.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kTitle = "Wi-Fi";
constexpr const char *kScanAgain = "Scan again";
constexpr const char *kNearby = "NETWORKS NEARBY";
constexpr const char *kScanning = "Scanning...";
constexpr const char *kNoNetworks = "No networks found";
constexpr const char *kConnected = "Connected";
constexpr const char *kPickNetwork = "Pick a network";
constexpr const char *kConnectTo = "Connect to %s";
constexpr const char *kPassword = "Password";
constexpr const char *kConnect = "Connect";
constexpr const char *kConnecting = "Connecting...";
constexpr const char *kConnectedTo = ICON_CHECK "  Connected to %s  %s";  // network, IP address
constexpr const char *kFailed = "Could not connect. Check the password and try again.";

// On-screen keyboard layouts
#define BKSP LV_SYMBOL_BACKSPACE
#define OK LV_SYMBOL_OK
const char *kLower[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", BKSP, "\n",
                        "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
                        "ABC", "z", "x", "c", "v", "b", "n", "m", ".", "-", "\n",
                        "1#", " ", OK, ""};
const char *kUpper[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", BKSP, "\n",
                        "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
                        "abc", "Z", "X", "C", "V", "B", "N", "M", ".", "-", "\n",
                        "1#", " ", OK, ""};
const char *kSpecial[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", BKSP, "\n",
                          "!", "@", "#", "$", "%", "&", "*", "(", ")", "\n",
                          "abc", "_", "+", "=", "?", "/", ":", ";", ",", "\"", "\n",
                          "ABC", " ", OK, ""};
#undef BKSP
#undef OK

constexpr lv_btnmatrix_ctrl_t F = LV_KEYBOARD_CTRL_BTN_FLAGS;
const lv_btnmatrix_ctrl_t kCtrl[] = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6 | F,
                                     4, 4, 4, 4, 4, 4, 4, 4, 4,
                                     6 | F, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                                     3 | F, 10, 4 | F};

constexpr int LIST_W = 290;
constexpr int BODY_Y = HEADER_H + GAP;
constexpr int BODY_H = SCREEN_H - BODY_Y - PAD;
constexpr int RIGHT_X = PAD + LIST_W + GAP;
constexpr int RIGHT_W = SCREEN_W - RIGHT_X - PAD;

// ---- State -------------------------------------------------------------------

lv_obj_t *list, *formTitle, *password, *eye, *status, *keyboard, *connectOpen;
bool scanning = false;
String selected;
bool selectedLocked = true;
WifiState lastState = WifiState::NotConfigured;
bool connectRequested = false;
std::vector<WifiNetwork> lastScan;

// ---- Code --------------------------------------------------------------------

void selectNetwork(const String &ssid, bool locked) {
  selected = ssid;
  selectedLocked = locked;
  lv_label_set_text(formTitle, strf(kConnectTo, ssid.c_str()).c_str());
  lv_textarea_set_text(password, "");
  lv_label_set_text(status, "");
  if (locked) {
    lv_obj_clear_flag(lv_obj_get_parent(password), LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(connectOpen, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(lv_obj_get_parent(password), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(connectOpen, LV_OBJ_FLAG_HIDDEN);
  }
}

void connect() {
  if (!selected.length()) return;
  wifi_connect(selected, selectedLocked ? String(lv_textarea_get_text(password)) : String(""));
  connectRequested = true;
  lastState = WifiState::NotConfigured;  // force a status redraw
}

struct Net {
  String ssid;
  bool locked;
};

void buildList(const std::vector<WifiNetwork> &nets) {
  const Theme &t = theme();
  if (&nets != &lastScan) lastScan = nets;
  lv_obj_clean(list);
  lv_obj_t *h = mk_label(list, &font_12, t.muted, kNearby);
  lv_obj_set_style_text_letter_space(h, 1, 0);
  lv_obj_set_style_pad_left(h, 10, 0);
  lv_obj_set_style_pad_ver(h, 6, 0);
  const String current = wifi_ssid();
  for (const WifiNetwork &n : nets) {
    const bool on = n.ssid == selected;
    lv_obj_t *b = mk_button(list, on ? t.raised : t.panel, 12);
    lv_obj_set_size(b, LIST_W - 16, 50);
    lv_obj_set_style_pad_hor(b, 12, 0);
    set_flex(b, LV_FLEX_FLOW_ROW, 10);
    // Signal strength: icon brightness steps with RSSI.
    mk_label(b, &font_16, n.rssi > -60 ? t.text : n.rssi > -72 ? t.muted : t.line, ICON_WIFI);
    lv_obj_t *col = mk_box(b, 0, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(col, 1);
    set_flex(col, LV_FLEX_FLOW_COLUMN, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *name = mk_label(col, &font_14b, t.text, n.ssid.c_str());
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, LIST_W - 110);
    if (n.ssid == current && wifi_state() == WifiState::Connected) mk_label(col, &font_12, t.good, kConnected);
    if (n.locked) mk_label(b, &font_14, t.muted, ICON_LOCK);
    on_click(b, [](lv_event_t *e) {
      const Net *n = static_cast<Net *>(lv_event_get_user_data(e));
      selectNetwork(n->ssid, n->locked);
      // Rebuild later: this button is part of the list being rebuilt.
      lv_async_call([](void *) { buildList(lastScan); }, nullptr);
    }, attach(b, Net{n.ssid, n.locked}));
  }
  if (nets.empty()) mk_label(list, &font_14, t.muted, kNoNetworks);
}

void startScan() {
  scanning = true;
  wifi_scan_start();
  lv_obj_clean(list);
  lv_obj_t *l = mk_label(list, &font_14, theme().muted, kScanning);
  lv_obj_set_style_pad_all(l, 10, 0);
}

}  // namespace

lv_obj_t *wifi_create() {
  const Theme &t = theme();
  lv_obj_t *scr = mk_screen();
  lv_obj_t *h = mk_back_header(scr, kTitle, Screen::Settings);
  lv_obj_t *scan = mk_button(h, t.card, TOUCH / 2);
  lv_obj_set_size(scan, LV_SIZE_CONTENT, TOUCH);
  lv_obj_set_style_pad_hor(scan, 16, 0);
  set_flex(scan, LV_FLEX_FLOW_ROW, 8);
  mk_label(scan, &font_14, t.text, ICON_REFRESH);
  mk_label(scan, &font_14b, t.text, kScanAgain);
  on_click(scan, [](lv_event_t *) { startScan(); });

  list = mk_scroll_column(scr, 2);
  lv_obj_set_style_bg_color(list, c(t.panel), 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(list, 16, 0);
  lv_obj_set_style_pad_all(list, 8, 0);
  lv_obj_set_pos(list, PAD, BODY_Y);
  lv_obj_set_size(list, LIST_W, BODY_H);

  lv_obj_t *form = mk_box(scr, t.card, 16);
  lv_obj_set_pos(form, RIGHT_X, BODY_Y);
  lv_obj_set_width(form, RIGHT_W);
  lv_obj_set_style_pad_hor(form, 16, 0);
  lv_obj_set_style_pad_ver(form, 14, 0);
  set_flex(form, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  formTitle = mk_label(form, &font_16s, t.text, kPickNetwork);
  lv_obj_t *row = mk_box(form, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_width(row, RIGHT_W - 32);
  set_flex(row, LV_FLEX_FLOW_ROW, 8);
  password = lv_textarea_create(row);
  lv_textarea_set_one_line(password, true);
  lv_textarea_set_password_mode(password, true);
  lv_textarea_set_placeholder_text(password, kPassword);
  lv_obj_set_height(password, 48);
  lv_obj_set_flex_grow(password, 1);
  lv_obj_set_style_text_font(password, &font_16, 0);
  lv_obj_set_style_bg_color(password, c(t.bg), 0);
  lv_obj_set_style_text_color(password, c(t.text), 0);
  lv_obj_set_style_border_color(password, c(t.accent), 0);
  lv_obj_set_style_border_width(password, 2, 0);
  lv_obj_set_style_radius(password, 12, 0);
  lv_obj_set_scrollbar_mode(password, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_ver(password, 12, 0);
  eye = mk_button(row, t.raised, 12);
  lv_obj_set_size(eye, 48, 48);
  lv_obj_center(mk_label(eye, &font_16, t.text, ICON_EYE));
  on_click(eye, [](lv_event_t *) {
    lv_textarea_set_password_mode(password, !lv_textarea_get_password_mode(password));
  });
  connectOpen = mk_text_button(form, kConnect, true, 140);
  lv_obj_add_flag(connectOpen, LV_OBJ_FLAG_HIDDEN);
  on_click(connectOpen, [](lv_event_t *) { connect(); });
  status = mk_label(form, &font_14, t.muted, "");

  keyboard = lv_keyboard_create(scr);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, kLower, kCtrl);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, kUpper, kCtrl);
  lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, kSpecial, kCtrl);
  lv_keyboard_set_textarea(keyboard, password);
  lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);  // the keyboard defaults to bottom-centre
  lv_obj_set_pos(keyboard, RIGHT_X, BODY_Y + 148);
  lv_obj_set_size(keyboard, RIGHT_W, BODY_H - 148);
  lv_obj_set_style_bg_color(keyboard, c(t.panel), 0);
  lv_obj_set_style_radius(keyboard, 16, 0);
  lv_obj_set_style_pad_all(keyboard, 8, 0);
  lv_obj_set_style_pad_gap(keyboard, 5, 0);
  lv_obj_set_style_border_width(keyboard, 0, 0);
  lv_obj_set_style_bg_color(keyboard, c(t.raised), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(keyboard, c(t.line), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(keyboard, c(t.raised), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(keyboard, c(t.text), LV_PART_ITEMS);
  lv_obj_set_style_text_font(keyboard, &font_16, LV_PART_ITEMS);
  lv_obj_set_style_radius(keyboard, 8, LV_PART_ITEMS);
  lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
  lv_obj_add_event_cb(keyboard, [](lv_event_t *) { connect(); }, LV_EVENT_READY, nullptr);
  return scr;
}

void wifi_on_show() {
  if (!selected.length()) {
    const String ssid = wifi_ssid();
    if (ssid.length()) selectNetwork(ssid, true);
  }
  startScan();
}

void wifi_tick() {
  if (scanning) {
    std::vector<WifiNetwork> nets;
    if (wifi_scan_results(nets)) {
      scanning = false;
      buildList(nets);
    }
  }
  const WifiState st = wifi_state();
  if (st == lastState) return;
  lastState = st;
  const Theme &t = theme();
  switch (st) {
    case WifiState::Connecting:
      lv_label_set_text(status, kConnecting);
      lv_obj_set_style_text_color(status, c(t.muted), 0);
      break;
    case WifiState::Connected:
      lv_label_set_text(status, strf(kConnectedTo, wifi_ssid().c_str(), wifi_ip().c_str()).c_str());
      lv_obj_set_style_text_color(status, c(t.good), 0);
      if (connectRequested) {
        connectRequested = false;
        lv_async_call([](void *) { ui_show(Screen::Settings); }, nullptr);
      }
      break;
    case WifiState::Failed:
      lv_label_set_text(status, kFailed);
      lv_obj_set_style_text_color(status, c(t.badFg), 0);
      connectRequested = false;
      break;
    default:
      lv_label_set_text(status, "");
      break;
  }
}
