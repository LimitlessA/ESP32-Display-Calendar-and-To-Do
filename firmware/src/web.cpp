#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "ota.h"
#include "settings.h"
#include "sync.h"
#include "web.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr uint8_t kMaxWrongPins = 5;  // then changes are locked...
constexpr uint32_t kLockMs = 60000;   // ...for a minute, so the PIN can't be guessed
constexpr int kMaxLabel = 20;         // longest account name
const uint32_t kAccountColors[] = {0x7BD88F, 0x6C8CFF, 0xF5A524, 0xE57CD8, 0x4FD1C5, 0xFF8A80};

// Replies
constexpr const char *kWrongPin = "Wrong PIN. Check the Accounts screen on the display.";
constexpr const char *kBadUrl = "The URL should look like https://script.google.com/macros/s/.../exec";
constexpr const char *kTooMany = "Too many accounts";
constexpr const char *kAdded = "Account added. It syncs within a few seconds.";
constexpr const char *kRemoved = "Account removed.";
constexpr const char *kLinkSaved = "Update link saved.";
constexpr const char *kBadIcs = "The calendar link should start with https:// or webcal://";
constexpr const char *kTooManyIcs = "Too many calendar links";
constexpr const char *kIcsAdded = "Calendar added. It shows up with the next sync.";
constexpr const char *kIcsRemoved = "Calendar removed.";
constexpr const char *kSyncSaved = "Sync interval saved.";
constexpr const char *kDeskSaved = "Desk settings saved. Turn the desk on in Settings > Features > Smart desk.";
constexpr const char *kInstalled = "Installed. The display is restarting.";
constexpr const char *kUpdateFailed = "Update failed: ";
constexpr const char *kInterrupted = "upload interrupted";
constexpr const char *kNotFound = "Not found";

// The setup page, top to bottom
const char kPageStart[] PROGMEM =
    "<!doctype html><html lang=en><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Calendar display</title><style>";
const char kStyle[] PROGMEM = R"(
body{margin:0;background:#101317;color:#F1F3F6;font:16px/1.45 system-ui,sans-serif}
main{max-width:720px;margin:0 auto;padding:24px 16px 48px}
h1{font-size:26px;margin:0 0 4px}h2{font-size:18px;margin:28px 0 10px}
p,li{color:#C3CAD3}a{color:#F5A524}
.card{background:#1C2229;border-radius:14px;padding:14px 16px;margin:10px 0}
.row{display:flex;align-items:center;gap:12px;flex-wrap:wrap}
.dot{width:14px;height:14px;border-radius:7px;flex:none}
.muted{color:#A9B1BC;font-size:14px}.err{color:#FFB4AB}.ok{color:#B8F0C4}
label{display:block;font-size:14px;color:#A9B1BC;margin:10px 0 4px}
input{width:100%;box-sizing:border-box;padding:12px;border-radius:10px;border:1px solid #3A434E;background:#101317;color:#F1F3F6;font:inherit}
button{padding:12px 18px;border-radius:22px;border:0;background:#F5A524;color:#101317;font:600 15px system-ui,sans-serif;cursor:pointer;margin-top:12px}
button.ghost{background:#262D36;color:#F1F3F6;margin:0}
.msg{background:#1E3A26;color:#B8F0C4;padding:10px 14px;border-radius:10px}
)";
const char kPageTitle[] PROGMEM =
    "</style></head><body><main><h1>Calendar display</h1><p class=muted>Firmware " FIRMWARE_VERSION " &middot; ";
const char kAccountsHeading[] PROGMEM = "<h2>Google accounts</h2>";
const char kNoAccounts[] PROGMEM = "<p>No accounts yet. Add one below.</p>";
constexpr const char *kNotSynced = "not synced yet";
constexpr const char *kSynced = "Synced";
constexpr const char *kRemove = "Remove";
const char kAddSection[] PROGMEM =
    "<h2>Add an account</h2><div class=card><ol>"
    "<li>Sign in to the Google account at <a href='https://script.google.com/create' target=_blank>script.google.com</a> and create a project.</li>"
    "<li>Paste <b>Code.gs</b> and <b>appsscript.json</b> from the repository's <code>apps-script</code> folder and set your own TOKEN.</li>"
    "<li>Run <b>preview</b> once to allow access, then Deploy &rsaquo; New deployment &rsaquo; Web app (Execute as: Me, Access: Anyone).</li>"
    "<li>Paste the web app URL and the token here.</li></ol>"
    "<form method=post action=/account/add>"
    "<label for=label>Name shown on the display</label><input id=label name=label placeholder='Personal' maxlength=20 required>"
    "<label for=url>Web app URL</label><input id=url name=url placeholder='https://script.google.com/macros/s/.../exec' required>"
    "<label for=token>Token</label><input id=token name=token required>"
    "<label for=pin1>PIN from the display</label><input id=pin1 name=pin inputmode=numeric required>"
    "<button>Add account</button></form></div>";
// Published calendars (ICS). Kept minimal: the page is getting a new layout.
const char kIcsHeading[] PROGMEM =
    "<h2>Other calendars (Outlook, iCloud, ICS)</h2>"
    "<p class=muted>Paste a published calendar link. Outlook: Settings &rsaquo; Calendar &rsaquo; Shared calendars &rsaquo; "
    "Publish a calendar &rsaquo; ICS. iCloud: share the calendar as a Public Calendar. "
    "The first Google account's script reads them, so add a Google account first.</p>";
const char kIcsAdd[] PROGMEM =
    "<div class=card><form method=post action=/ics/add>"
    "<label for=ilabel>Name shown on the display</label><input id=ilabel name=label placeholder='Work (Outlook)' maxlength=20 required>"
    "<label for=iurl>Calendar link (.ics)</label><input id=iurl name=url placeholder='https://outlook.office365.com/owa/calendar/.../calendar.ics' required>"
    "<label for=pin5>PIN from the display</label><input id=pin5 name=pin inputmode=numeric required>"
    "<button>Add calendar</button></form></div>";
// Smart desk: address and the desk web UI login. Values go between the parts.
const char kDeskStart[] PROGMEM =
    "<h2>Smart desk</h2><div class=card><p>The display talks to the desk's own web UI. "
    "Without a login only the memory buttons and Stop work; raise, lower, the sit/stand timer and "
    "away mode need a desk account with the <b>user</b> role.</p>"
    "<form method=post action=/settings/desk>"
    "<label for=dhost>Desk address</label><input id=dhost name=host placeholder='smartdesk-a43c.local' value='";
const char kDeskUser[] PROGMEM = "'><label for=duser>Desk username (optional)</label><input id=duser name=user autocomplete=off value='";
const char kDeskEnd[] PROGMEM =
    "'><label for=dpass>Desk password</label><input id=dpass name=pass type=password autocomplete=new-password "
    "placeholder='unchanged if empty'>"
    "<label for=pin4>PIN from the display</label><input id=pin4 name=pin inputmode=numeric required>"
    "<button>Save desk</button></form></div>";
const char kFirmwareSection[] PROGMEM =
    "<h2>Firmware update</h2><div class=card><p>New versions install by themselves (Settings &rsaquo; Auto-update). "
    "To install one by hand, upload a <b>firmware.bin</b> from a GitHub release or build. "
    "The display restarts by itself when it is done.</p>"
    "<form method=post enctype=multipart/form-data id=up>"
    "<label for=fw>Firmware file</label><input id=fw type=file name=fw accept='.bin' required>"
    "<label for=pin2>PIN from the display</label><input id=pin2 name=pin inputmode=numeric required>"
    "<button>Upload and install</button></form>"
    "<script>up.onsubmit=function(){up.action='/update?pin='+encodeURIComponent(pin2.value)}</script></div>"
    "<div class=card><p>Where the display looks for new versions (a <code>manifest.json</code>). "
    "Leave it as it is unless you host the firmware somewhere else.</p>"
    "<form method=post action=/settings/update-url>"
    "<label for=uurl>Update link</label><input id=uurl name=url value='";
const char kPageEnd[] PROGMEM =
    "' placeholder='https://.../manifest.json'>"
    "<label for=pin3>PIN from the display</label><input id=pin3 name=pin inputmode=numeric required>"
    "<button>Save link</button></form></div></main></body></html>";

// ---- State -------------------------------------------------------------------

WebServer server(80);
String pin;
bool uploadAuthorized = false;
uint8_t wrongPins = 0;
uint32_t lockedUntil = 0;

// ---- Code --------------------------------------------------------------------

String esc(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

String hex(uint32_t c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06lX", (unsigned long)(c & 0xFFFFFF));
  return buf;
}

// Five wrong PINs lock changes for a minute, so the PIN can't be guessed.
bool checkPin(const String &given) {
  if (millis() < lockedUntil) return false;
  if (given == pin) {
    wrongPins = 0;
    return true;
  }
  if (++wrongPins >= kMaxWrongPins) {
    wrongPins = 0;
    lockedUntil = millis() + kLockMs;
  }
  return false;
}

bool pinOk() {
  if (checkPin(server.arg("pin"))) return true;
  server.send(403, "text/plain", kWrongPin);
  return false;
}

void redirectHome(const String &msg = "") {
  server.sendHeader("Location", msg.length() ? "/?msg=" + msg : "/");
  server.send(303);
}


void handleRoot() {
  const Settings s = settings_get();
  Snapshot snap;
  sync_get_if_newer(UINT32_MAX, snap);

  String h;
  h.reserve(8000);
  h += FPSTR(kPageStart);
  h += FPSTR(kStyle);
  h += FPSTR(kPageTitle);
  h += esc(WiFi.localIP().toString());
  h += F("</p>");
  if (server.hasArg("msg")) h += "<p class=msg>" + esc(server.arg("msg")) + "</p>";

  h += FPSTR(kAccountsHeading);
  if (s.accounts.empty()) h += FPSTR(kNoAccounts);
  for (size_t i = 0; i < s.accounts.size(); i++) {
    const Account &a = s.accounts[i];
    const AccountStatus st = i < snap.accounts.size() ? snap.accounts[i] : AccountStatus();
    h += "<div class='card row'><span class=dot style='background:" + hex(a.color) + "'></span><div style='flex:1;min-width:200px'><b>" +
         esc(a.label) + "</b><div class=muted>" + esc(st.email.length() ? st.email : String(kNotSynced)) + "</div>";
    if (st.error.length()) h += "<div class=err>" + esc(st.error) + "</div>";
    else if (st.syncedAt) h += "<div class=ok>" + String(kSynced) + "</div>";
    h += "</div><form method=post action=/account/remove class=row><input type=hidden name=i value=" + String(i) +
         "><input name=pin placeholder=PIN inputmode=numeric style='width:110px' aria-label=PIN required>"
         "<button class=ghost>" + String(kRemove) + "</button></form></div>";
  }

  h += FPSTR(kAddSection);

  h += FPSTR(kIcsHeading);
  for (size_t i = 0; i < s.ics.size(); i++) {
    const IcsCalendar &c = s.ics[i];
    const IcsStatus st = i < snap.ics.size() ? snap.ics[i] : IcsStatus();
    h += "<div class='card row'><span class=dot style='background:" + hex(c.color) +
         "'></span><div style='flex:1;min-width:200px;overflow-wrap:anywhere'><b>" + esc(c.label) + "</b><div class=muted>" +
         esc(c.url) + "</div>";
    if (st.error.length()) h += "<div class=err>" + esc(st.error) + "</div>";
    else if (st.synced) h += "<div class=ok>" + String(kSynced) + " &middot; " + String((int)st.events) + " events</div>";
    h += "</div><form method=post action=/ics/remove class=row><input type=hidden name=i value=" + String(i) +
         "><input name=pin placeholder=PIN inputmode=numeric style='width:110px' aria-label=PIN required>"
         "<button class=ghost>" + String(kRemove) + "</button></form></div>";
  }
  h += FPSTR(kIcsAdd);
  h += FPSTR(kDeskStart);
  h += esc(s.deskHost);
  h += FPSTR(kDeskUser);
  h += esc(s.deskUser);
  h += FPSTR(kDeskEnd);
  h += FPSTR(kFirmwareSection);
  h += esc(s.updateUrl);
  h += FPSTR(kPageEnd);
  server.send(200, "text/html", h);
}

void handleAddAccount() {
  if (!pinOk()) return;
  Settings s = settings_get();
  const String url = server.arg("url");
  if (!url.startsWith("https://script.google.com/") || !url.endsWith("/exec")) {
    server.send(400, "text/plain", kBadUrl);
    return;
  }
  if (s.accounts.size() >= MAX_ACCOUNTS) {
    server.send(400, "text/plain", kTooMany);
    return;
  }
  String label = server.arg("label");
  label.trim();
  s.accounts.push_back({label.substring(0, kMaxLabel), url, server.arg("token"),
                        kAccountColors[s.accounts.size() % (sizeof(kAccountColors) / sizeof(kAccountColors[0]))]});
  settings_save(s);
  sync_refresh();
  redirectHome(kAdded);
}

void handleRemoveAccount() {
  if (!pinOk()) return;
  Settings s = settings_get();
  const size_t i = server.arg("i").toInt();
  if (i < s.accounts.size()) {
    s.accounts.erase(s.accounts.begin() + i);
    settings_save(s);
  }
  redirectHome(kRemoved);
}

void handleAddIcs() {
  if (!pinOk()) return;
  Settings s = settings_get();
  String url = server.arg("url");
  url.trim();
  if (!url.startsWith("https://") && !url.startsWith("webcal://") && !url.startsWith("http://")) {
    server.send(400, "text/plain", kBadIcs);
    return;
  }
  if (s.ics.size() >= kMaxIcs) {
    server.send(400, "text/plain", kTooManyIcs);
    return;
  }
  String label = server.arg("label");
  label.trim();
  const size_t n = sizeof(kAccountColors) / sizeof(kAccountColors[0]);
  s.ics.push_back({label.substring(0, kMaxLabel), url, kAccountColors[(s.accounts.size() + s.ics.size()) % n]});
  settings_save(s);
  sync_refresh();
  redirectHome(kIcsAdded);
}

void handleRemoveIcs() {
  if (!pinOk()) return;
  Settings s = settings_get();
  const size_t i = server.arg("i").toInt();
  if (i < s.ics.size()) {
    s.ics.erase(s.ics.begin() + i);
    settings_save(s);
  }
  redirectHome(kIcsRemoved);
}

// Sync interval in minutes (Settings > Sync on the display): minutes=2..60.
void handleSyncInterval() {
  if (!pinOk()) return;
  Settings s = settings_get();
  s.refreshMinutes = constrain((int)server.arg("minutes").toInt(), 1, 60);
  settings_save(s);
  redirectHome(kSyncSaved);
}

void handleUpdateUrl() {
  if (!pinOk()) return;
  Settings s = settings_get();
  s.updateUrl = server.arg("url");
  s.updateUrl.trim();
  settings_save(s);
  redirectHome(kLinkSaved);
}

void handleDesk() {
  if (!pinOk()) return;
  Settings s = settings_get();
  s.deskHost = server.arg("host");
  s.deskHost.trim();
  s.deskUser = server.arg("user");
  s.deskUser.trim();
  if (!s.deskUser.length()) s.deskPassword = "";
  else if (server.arg("pass").length()) s.deskPassword = server.arg("pass");
  settings_save(s);
  redirectHome(kDeskSaved);
}

void handleUploadDone() {
  if (!uploadAuthorized) {
    server.send(403, "text/plain", kWrongPin);
    return;
  }
  const OtaStatus st = ota_status();
  if (st.state == OtaState::Done) {
    server.send(200, "text/plain", kInstalled);
  } else {
    server.send(500, "text/plain", String(kUpdateFailed) + st.message);
  }
}

void handleUpload() {
  HTTPUpload &up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      uploadAuthorized = checkPin(server.arg("pin"));
      if (uploadAuthorized) ota_upload_start(0);
      break;
    case UPLOAD_FILE_WRITE:
      if (uploadAuthorized) ota_upload_write(up.buf, up.currentSize);
      break;
    case UPLOAD_FILE_END:
      if (uploadAuthorized) ota_upload_finish();
      break;
    case UPLOAD_FILE_ABORTED:
      if (uploadAuthorized) ota_upload_abort(kInterrupted);
      break;
  }
}

void webTask(void *) {
  bool mdns = false;
  for (;;) {
    if (!mdns && WiFi.status() == WL_CONNECTED) {
      mdns = MDNS.begin(HOSTNAME);
      if (mdns) MDNS.addService("http", "tcp", 80);
    }
    server.handleClient();
    delay(2);
  }
}

}  // namespace

void web_begin() {
  pin = String(100000 + esp_random() % 900000);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/account/add", HTTP_POST, handleAddAccount);
  server.on("/account/remove", HTTP_POST, handleRemoveAccount);
  server.on("/ics/add", HTTP_POST, handleAddIcs);
  server.on("/ics/remove", HTTP_POST, handleRemoveIcs);
  server.on("/settings/sync", HTTP_POST, handleSyncInterval);
  server.on("/settings/update-url", HTTP_POST, handleUpdateUrl);
  server.on("/settings/desk", HTTP_POST, handleDesk);
  server.on("/update", HTTP_POST, handleUploadDone, handleUpload);
  server.onNotFound([] { server.send(404, "text/plain", kNotFound); });
  server.begin();
  xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0);
}

String web_pin() {
  return pin;
}

String web_url() {
  return "http://" HOSTNAME ".local";
}
