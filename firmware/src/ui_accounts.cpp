// Settings > Accounts, layout B "master / detail" (mockups round 5): a compact
// list of Google accounts, published (ICS) calendars such as Outlook or iCloud
// and the smart desk on the left; the selected one's status and actions on the
// right. New ones are added on the setup page (QR code and PIN in "+ Add").
#include "desk.h"
#include "sync.h"
#include "ui_internal.h"
#include "web.h"
#include "wifi_mgr.h"

// The desk settings overlay (ui_desk.cpp on the desk branch). Until that is
// merged, "Edit login" falls back to Settings > Features.
void desk_settings_show();
__attribute__((weak)) void desk_settings_show() { ui_show(Screen::Features); }

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kTitle = "Settings > Accounts";
constexpr const char *kGroupGoogle = "GOOGLE";
constexpr const char *kGroupIcs = "CALENDARS";
constexpr const char *kGroupDesk = "DESK";
constexpr const char *kAdd = "+ Add account / calendar / desk";
constexpr const char *kKindGoogle = "Google";
constexpr const char *kKindIcs = "Calendar link (ICS)";
constexpr const char *kKindDesk = "Smart desk";
constexpr const char *kNoGoogle = "No Google account";
constexpr const char *kNeedsAttention = "needs attention";
constexpr const char *kNotSynced = "Not synced yet";
constexpr const char *kWaiting = "Waiting for the first sync";
constexpr const char *kSyncedAt = "Synced %s";  // %s = time
constexpr const char *kEventsSynced = "%d events, synced";
constexpr const char *kNeedsGoogle = "Needs a Google account: its script reads the link";
constexpr const char *kKeyAccount = "Account";
constexpr const char *kKeyStatus = "Status";
constexpr const char *kKeyCalTasks = "Calendars / tasks";
constexpr const char *kCalTasks = "%d of %d / %d";
constexpr const char *kKeyColour = "Colour";
constexpr const char *kKeyLink = "Link";
constexpr const char *kKeyReadBy = "Read by";
constexpr const char *kKeyAddress = "Address";
constexpr const char *kKeyLogin = "Login";
constexpr const char *kLoginSet = "%s \xC2\xB7 ********";
constexpr const char *kNoLogin = "None (memory buttons only)";
constexpr const char *kDeskOff = "Off (Settings > Features)";
constexpr const char *kDeskConnected = "Connected";
constexpr const char *kDeskUserRole = "Connected - user role";
constexpr const char *kDeskLoginRefused = "Connected - login refused";
constexpr const char *kDeskOffline = "Not answering";
constexpr const char *kFix = "Fix on calendar.local";
constexpr const char *kCalendars = "Calendars";
constexpr const char *kSyncNow = "Sync now";
constexpr const char *kSyncing = "Syncing...";
constexpr const char *kRemove = "Remove";
constexpr const char *kTest = "Test connection";
constexpr const char *kEditLogin = "Edit login";
constexpr const char *kForget = "Forget";
constexpr const char *kTesting = "Desk: %s";
constexpr const char *kEmpty = "Nothing here yet. Tap \"+ Add\" to connect a Google account, an Outlook or iCloud calendar, or the desk.";
constexpr const char *kAddTitle = "Add an account, calendar or desk";
constexpr const char *kAddSteps =
    "1. Scan the code, or open the address on a phone or PC on this Wi-Fi.\n"
    "2. Google: set up the script in that account and paste its link.\n"
    "    Outlook / iCloud: paste the published calendar link (ICS).\n"
    "    Desk: enter its address and login.\n"
    "3. Confirm with this PIN:";
constexpr const char *kClose = "Close";
constexpr const char *kRemoveTitle = "Remove %s?";
constexpr const char *kRemoveText = "Its meetings and tasks leave the display. Nothing is deleted from Google.";
constexpr const char *kRemoveIcsText = "Its events leave the display. The calendar itself is not changed.";
constexpr const char *kForgetTitle = "Forget the desk?";
constexpr const char *kForgetText = "The display stops controlling the desk and forgets its login. The desk keeps its settings.";
constexpr const char *kCancel = "Cancel";

constexpr int LIST_W = 300;
constexpr int BODY_Y = HEADER_H;
constexpr int BODY_H = SCREEN_H - BODY_Y;
constexpr int DETAIL_X = LIST_W + 24;
constexpr int DETAIL_W = SCREEN_W - DETAIL_X - 24;
constexpr int ITEM_H = 56;
constexpr int QR_SIZE = 150;

// ---- State -------------------------------------------------------------------

enum class Kind : uint8_t { Google, Ics, Desk };
struct Sel {
  Kind kind;
  uint8_t index;
};
Sel sel{Kind::Google, 0};
lv_obj_t *list, *detail;
lv_obj_t *addLayer = nullptr, *confirmLayer = nullptr;
Sel pendingRemove;

// ---- Code --------------------------------------------------------------------

bool exists(const Sel &s) {
  switch (s.kind) {
    case Kind::Google: return s.index < g_settings.accounts.size();
    case Kind::Ics: return s.index < g_settings.ics.size();
    case Kind::Desk: return true;
  }
  return false;
}

// Keeps the selection on something that is still there.
void fixSelection() {
  if (exists(sel)) return;
  if (g_settings.accounts.size()) sel = {Kind::Google, 0};
  else if (g_settings.ics.size()) sel = {Kind::Ics, 0};
  else sel = {Kind::Desk, 0};
}

AccountStatus googleStatus(uint8_t i) {
  return i < g_snap.accounts.size() ? g_snap.accounts[i] : AccountStatus();
}

IcsStatus icsStatus(uint8_t i) {
  return i < g_snap.ics.size() ? g_snap.ics[i] : IcsStatus();
}

// "outlook.office365.com" out of a link: enough to tell links apart.
String hostOf(const String &url) {
  int from = url.indexOf(':');
  from = from < 0 ? 0 : from + 1;
  while (from < (int)url.length() && url[from] == '/') from++;
  const int to = url.indexOf('/', from);
  return url.substring(from, to < 0 ? (int)url.length() : to);
}

// "smartdesk-a43c" out of "smartdesk-a43c.local".
String deskName() {
  String h = hostOf(g_settings.deskHost);
  if (h.endsWith(".local")) h = h.substring(0, h.length() - 6);
  return h;
}

String icsKind(const String &url) {
  const char *u = url.c_str();
  if (strstr(u, "icloud.com")) return "iCloud";
  if (strstr(u, "outlook.") || strstr(u, "office365") || strstr(u, "live.com")) return "Outlook";
  return hostOf(url);
}

String icsName(uint8_t i) {
  const IcsCalendar &ic = g_settings.ics[i];
  if (ic.label.length()) return ic.label;
  const IcsStatus st = icsStatus(i);
  return st.name.length() ? st.name : hostOf(ic.url);
}

void select(lv_event_t *e) {
  const uintptr_t v = (uintptr_t)lv_event_get_user_data(e);
  sel = {(Kind)(v >> 8), (uint8_t)(v & 0xFF)};
  accounts_refresh();
}

void group(const char *text) {
  lv_obj_t *l = mk_label(list, &font_12, theme().muted, text);
  lv_obj_set_style_text_letter_space(l, 1, 0);
  lv_obj_set_style_pad_left(l, 8, 0);
  lv_obj_set_style_pad_top(l, 8, 0);
}

void item(Kind kind, uint8_t index, uint32_t color, const String &name, const String &sub, bool bad) {
  const Theme &t = theme();
  const bool on = sel.kind == kind && sel.index == index;
  lv_obj_t *b = mk_button(list, on ? t.raised : t.header, 10);
  if (!on) lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
  lv_obj_set_size(b, LIST_W - 16, ITEM_H);
  lv_obj_set_style_pad_hor(b, 10, 0);
  set_flex(b, LV_FLEX_FLOW_ROW, 10);
  mk_dot(b, color, 12);
  lv_obj_t *col = mk_box(b, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_grow(col, 1);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 0, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_t *n = mk_label(col, &font_16, t.text, name.c_str());
  lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
  lv_obj_set_width(n, LIST_W - 70);
  lv_obj_t *s = mk_label(col, &font_12, bad ? t.badFg : t.muted, sub.c_str());
  lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s, LIST_W - 70);
  on_click(b, select, (void *)(((uintptr_t)kind << 8) | index));
}

// One "key ... value" line with a separator under it.
lv_obj_t *kv(const char *key, const String &value, uint32_t color) {
  const Theme &t = theme();
  lv_obj_t *row = mk_box(detail, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_size(row, DETAIL_W, 44);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(row, c(t.card), 0);
  lv_obj_set_style_border_width(row, 1, 0);
  set_flex(row, LV_FLEX_FLOW_ROW, 16, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
  mk_label(row, &font_16, t.muted, key);
  lv_obj_t *v = mk_label(row, &font_16, color, value.c_str());
  lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(v, DETAIL_W - 170, 0);
  return row;
}

void colourRow(uint32_t color) {
  lv_obj_t *row = kv(kKeyColour, "", theme().text);
  mk_dot(row, color, 14);
}

lv_obj_t *actions() {
  lv_obj_t *row = mk_box(detail, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_width(row, DETAIL_W);
  lv_obj_set_style_pad_top(row, 10, 0);
  set_flex(row, LV_FLEX_FLOW_ROW_WRAP, 10);
  return row;
}

lv_obj_t *dangerButton(lv_obj_t *parent, const char *text) {
  lv_obj_t *b = mk_text_button(parent, text, true);
  lv_obj_set_style_bg_color(b, c(theme().badBg), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(b, 0), c(theme().badFg), 0);
  return b;
}

void syncNow(lv_event_t *) {
  sync_refresh();
  toast(kSyncing);
}

// "+ Add" and "Fix": the setup page's address as a QR code, and the PIN.
void showAdd(lv_event_t *) {
  const Theme &t = theme();
  lv_obj_t *layer = mk_overlay(true, &addLayer);
  lv_obj_t *card = mk_box(layer, t.card, 20);
  lv_obj_set_width(card, 600);
  lv_obj_set_style_pad_all(card, 20, 0);
  set_flex(card, LV_FLEX_FLOW_ROW, 20, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);
  lv_obj_t *qrBox = mk_box(card, 0xFFFFFF, 12);
  lv_obj_set_size(qrBox, QR_SIZE + 16, QR_SIZE + 16);
  lv_obj_set_style_pad_all(qrBox, 8, 0);
  const String ip = wifi_ip();
  // The QR code points at the IP address: ".local" names don't work on every phone.
  const String url = ip.length() ? String("http://") + ip : web_url();
  lv_obj_t *qr = lv_qrcode_create(qrBox, QR_SIZE, lv_color_black(), lv_color_white());
  lv_qrcode_update(qr, url.c_str(), url.length());
  lv_obj_center(qr);
  lv_obj_t *col = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_width(col, 600 - 40 - QR_SIZE - 16 - 20);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  mk_label(col, &font_16s, t.text, kAddTitle);
  mk_label(col, &font_16s, t.accent, url.c_str());
  lv_obj_t *steps = mk_label(col, &font_12, t.muted, kAddSteps);
  lv_obj_set_width(steps, lv_pct(100));
  lv_label_set_long_mode(steps, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(steps, 3, 0);
  lv_obj_t *pin = mk_label(col, &font_26b, t.accent, web_pin().c_str());
  lv_obj_set_style_text_letter_space(pin, 4, 0);
  on_click(mk_text_button(col, kClose, false, 120), [](lv_event_t *) { close_overlay(&addLayer); });
}

void confirm(const String &title, const char *text, const char *action) {
  const Theme &t = theme();
  lv_obj_t *layer = mk_overlay(false, &confirmLayer);
  lv_obj_t *card = mk_box(layer, t.card, 24);
  lv_obj_set_width(card, 480);
  lv_obj_set_style_pad_all(card, 24, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 14, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);
  mk_label(card, &font_20b, t.text, title.c_str());
  lv_obj_t *msg = mk_label(card, &font_14, t.muted, text);
  lv_obj_set_width(msg, 432);
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
  lv_obj_t *row = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_width(row, 432);
  set_flex(row, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
  on_click(mk_text_button(row, kCancel, false, 120), [](lv_event_t *) { close_overlay(&confirmLayer); });
  on_click(dangerButton(row, action), [](lv_event_t *) {
    close_overlay(&confirmLayer);
    Settings s = g_settings;
    const Sel r = pendingRemove;
    if (r.kind == Kind::Google && r.index < s.accounts.size()) s.accounts.erase(s.accounts.begin() + r.index);
    if (r.kind == Kind::Ics && r.index < s.ics.size()) s.ics.erase(s.ics.begin() + r.index);
    if (r.kind == Kind::Desk) {
      s.desk = false;
      s.deskUser = "";
      s.deskPassword = "";
    }
    ui_save_settings(s);
    lv_async_call([](void *) { accounts_refresh(); }, nullptr);
  });
}

void askRemove(lv_event_t *) {
  pendingRemove = sel;
  if (sel.kind == Kind::Desk) {
    confirm(kForgetTitle, kForgetText, kForget);
  } else if (sel.kind == Kind::Ics) {
    confirm(strf(kRemoveTitle, icsName(sel.index).c_str()), kRemoveIcsText, kRemove);
  } else {
    confirm(strf(kRemoveTitle, g_settings.accounts[sel.index].label.c_str()), kRemoveText, kRemove);
  }
}

String deskState(uint32_t &color) {
  const Theme &t = theme();
  if (!g_settings.desk) {
    color = t.muted;
    return kDeskOff;
  }
  const DeskStatus st = desk_status();
  if (!st.online) {
    color = t.badFg;
    return st.error.length() ? st.error : String(kDeskOffline);
  }
  color = t.good;
  if (!st.hasLogin) return kDeskConnected;
  if (st.loggedIn) return kDeskUserRole;
  color = t.warnFg;
  return kDeskLoginRefused;
}

void heading(const String &name, const char *kind) {
  const Theme &t = theme();
  lv_obj_t *row = mk_box(detail, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_width(row, DETAIL_W);
  lv_obj_set_style_pad_bottom(row, 6, 0);
  set_flex(row, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END);
  lv_obj_t *n = mk_label(row, &font_26b, t.text, name.c_str());
  lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(n, DETAIL_W - 190, 0);
  mk_label(row, &font_14, t.muted, kind);
}

void googleDetail(uint8_t i) {
  const Theme &t = theme();
  const Account &a = g_settings.accounts[i];
  const AccountStatus st = googleStatus(i);
  heading(a.label, kKindGoogle);
  kv(kKeyAccount, st.email.length() ? st.email : String(kNotSynced), t.text);
  const bool bad = st.error.length() > 0;
  kv(kKeyStatus, bad ? st.error : st.syncedAt ? strf(kSyncedAt, fmt_time(st.syncedAt).c_str()) : String(kWaiting),
     bad ? t.badFg : st.syncedAt ? t.good : t.muted);
  int shown = 0, tasks = 0;
  for (const CalendarInfo &cal : st.calendars) shown += cal.shown;
  for (const Task &task : g_snap.tasks) tasks += task.account == i;
  kv(kKeyCalTasks, strf(kCalTasks, shown, (int)st.calendars.size(), tasks), t.text);
  colourRow(a.color);
  lv_obj_t *row = actions();
  if (bad) on_click(mk_text_button(row, kFix, true), showAdd);
  else on_click(mk_text_button(row, kCalendars, false), [](lv_event_t *) { calendars_dialog_show(sel.index); });
  on_click(mk_text_button(row, kSyncNow, false), syncNow);
  on_click(dangerButton(row, kRemove), askRemove);
}

void icsDetail(uint8_t i) {
  const Theme &t = theme();
  const IcsCalendar &ic = g_settings.ics[i];
  const IcsStatus st = icsStatus(i);
  heading(icsName(i), kKindIcs);
  kv(kKeyLink, hostOf(ic.url), t.text);
  const bool google = !g_settings.accounts.empty();
  const bool bad = !google || st.error.length() > 0;
  kv(kKeyStatus, !google ? String(kNeedsGoogle) : bad ? st.error
                 : st.synced ? strf(kEventsSynced, (int)st.events) : String(kWaiting),
     bad ? t.badFg : st.synced ? t.good : t.muted);
  kv(kKeyReadBy, google ? g_settings.accounts[0].label : String(kNoGoogle), google ? t.text : t.badFg);
  colourRow(ic.color);
  lv_obj_t *row = actions();
  if (bad) on_click(mk_text_button(row, kFix, true), showAdd);
  on_click(mk_text_button(row, kSyncNow, false), syncNow);
  on_click(dangerButton(row, kRemove), askRemove);
}

void deskDetail() {
  const Theme &t = theme();
  heading(deskName(), kKindDesk);
  kv(kKeyAddress, g_settings.deskHost, t.text);
  kv(kKeyLogin, g_settings.deskUser.length() ? strf(kLoginSet, g_settings.deskUser.c_str()) : String(kNoLogin), t.text);
  uint32_t color;
  const String state = deskState(color);
  kv(kKeyStatus, state, color);
  lv_obj_t *row = actions();
  on_click(mk_text_button(row, kTest, true), [](lv_event_t *) {
    uint32_t color;
    toast(strf(kTesting, deskState(color).c_str()));
    accounts_refresh();
  });
  on_click(mk_text_button(row, kEditLogin, false), [](lv_event_t *) { desk_settings_show(); });
  on_click(dangerButton(row, kForget), askRemove);
}

}  // namespace

lv_obj_t *accounts_create() {
  const Theme &t = theme();
  lv_obj_t *scr = mk_screen();
  mk_back_header(scr, kTitle, Screen::Settings);

  list = mk_scroll_column(scr, 2);
  lv_obj_set_style_bg_color(list, c(t.header), 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(list, 8, 0);
  lv_obj_set_pos(list, 0, BODY_Y);
  lv_obj_set_size(list, LIST_W, BODY_H);

  detail = mk_box(scr, 0, 0);
  lv_obj_set_style_bg_opa(detail, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(detail, DETAIL_X, BODY_Y + 18);
  lv_obj_set_size(detail, DETAIL_W, BODY_H - 18);
  set_flex(detail, LV_FLEX_FLOW_COLUMN, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  return scr;
}

void accounts_refresh() {
  const Theme &t = theme();
  fixSelection();

  const lv_coord_t scroll = lv_obj_get_scroll_y(list);
  lv_obj_clean(list);
  group(kGroupGoogle);
  for (uint8_t i = 0; i < g_settings.accounts.size(); i++) {
    const Account &a = g_settings.accounts[i];
    const AccountStatus st = googleStatus(i);
    const bool bad = st.error.length() > 0;
    item(Kind::Google, i, a.color, a.label, bad ? String(kNeedsAttention) : st.email.length() ? st.email : String(kNotSynced), bad);
  }
  group(kGroupIcs);
  for (uint8_t i = 0; i < g_settings.ics.size(); i++) {
    const IcsStatus st = icsStatus(i);
    const bool bad = g_settings.accounts.empty() || st.error.length() > 0;
    item(Kind::Ics, i, g_settings.ics[i].color, icsName(i), bad ? String(kNeedsAttention) : icsKind(g_settings.ics[i].url), bad);
  }
  group(kGroupDesk);
  uint32_t deskColor;
  const String deskText = deskState(deskColor);
  item(Kind::Desk, 0, g_settings.desk ? deskColor : t.muted, deskName(), deskText,
       g_settings.desk && deskColor == t.badFg);

  lv_obj_t *add = mk_button(list, t.header, 10);
  lv_obj_set_style_bg_opa(add, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(add, c(t.line), 0);
  lv_obj_set_style_border_width(add, 2, 0);
  lv_obj_set_size(add, LIST_W - 16, 48);
  lv_obj_set_style_pad_top(add, 0, 0);
  lv_obj_center(mk_label(add, &font_14b, t.accent, kAdd));
  on_click(add, showAdd);
  lv_obj_update_layout(list);
  lv_obj_scroll_to_y(list, scroll, LV_ANIM_OFF);  // keep the place across the minute refresh

  lv_obj_clean(detail);
  if (sel.kind == Kind::Google) googleDetail(sel.index);
  else if (sel.kind == Kind::Ics) icsDetail(sel.index);
  else deskDetail();
  if (g_settings.accounts.empty() && g_settings.ics.empty() && !g_settings.desk) {
    lv_obj_t *hint = mk_label(detail, &font_14, t.muted, kEmpty);
    lv_obj_set_width(hint, DETAIL_W);
    lv_obj_set_style_pad_top(hint, 18, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  }
}
