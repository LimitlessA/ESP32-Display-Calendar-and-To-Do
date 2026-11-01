#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#include "config.h"
#include "desk.h"
#include "ota.h"
#include "settings.h"
#include "sync.h"
#include "web.h"
#include "web_page.h"
#include "wifi_mgr.h"
#include "harness.h"

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
constexpr const char *kBadIcs = "The calendar link should start with https://, http:// or webcal://";
constexpr const char *kTooManyIcs = "Too many calendar links";
constexpr const char *kIcsAdded = "Calendar added. It shows up with the next sync.";
constexpr const char *kIcsRemoved = "Calendar removed.";
constexpr const char *kSyncSaved = "Sync interval saved.";
constexpr const char *kDeskSaved = "Desk settings saved. Turn the desk on in Settings > Features > Smart desk.";
constexpr const char *kInstalled = "Installed. The display is restarting.";
constexpr const char *kUpdateFailed = "Update failed: ";
constexpr const char *kInterrupted = "upload interrupted";
constexpr const char *kNotFound = "Not found";

constexpr const char *kPinOk = "PIN OK.";
constexpr const char *kLocked = "Too many wrong PINs. Wait a minute, then try again.";
constexpr const char *kTesting = "Testing the desk...";

// ---- State -------------------------------------------------------------------

WebServer server(80);
String pin;
bool uploadAuthorized = false;
uint8_t wrongPins = 0;
uint32_t lockedUntil = 0;

// ---- Code --------------------------------------------------------------------

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
  if (millis() < lockedUntil) server.send(429, "text/plain", kLocked);
  else server.send(403, "text/plain", kWrongPin);
  return false;
}

// The page posts with fetch() and shows the reply text next to the form.
void reply(const char *msg) {
  server.send(200, "text/plain", msg);
}

void sendJson(const JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

// The page itself never changes at run time (web/index.html, gzipped into web_page.h):
// everything it shows comes from /state.
void handleRoot() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", reinterpret_cast<PGM_P>(kWebPage), kWebPageSize);
}

const char *otaStateName(OtaState s) {
  switch (s) {
    case OtaState::Checking: return "checking";
    case OtaState::UpToDate: return "uptodate";
    case OtaState::Available: return "available";
    case OtaState::Installing: return "installing";
    case OtaState::Done: return "done";
    case OtaState::Failed: return "failed";
    default: return "idle";
  }
}

// What the page shows. No PIN needed, like the old page; tokens and passwords are left out.
void handleState() {
  const Settings s = settings_get();
  Snapshot snap;
  sync_get_if_newer(UINT32_MAX, snap);
  const DeskStatus desk = desk_status();
  const OtaStatus ota = ota_status();
  const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);

  JsonDocument doc;
  doc["v"] = FIRMWARE_VERSION;
  doc["host"] = HOSTNAME ".local";
  doc["maxAccounts"] = MAX_ACCOUNTS;
  doc["maxImage"] = slot ? slot->size : 0;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ok"] = WiFi.status() == WL_CONNECTED;
  wifi["ssid"] = wifi_ssid();
  wifi["rssi"] = wifi_rssi();
  wifi["ip"] = wifi_ip();

  JsonArray accounts = doc["accounts"].to<JsonArray>();
  for (size_t i = 0; i < s.accounts.size(); i++) {
    const AccountStatus st = i < snap.accounts.size() ? snap.accounts[i] : AccountStatus();
    JsonObject a = accounts.add<JsonObject>();
    a["label"] = s.accounts[i].label;
    a["color"] = hex(s.accounts[i].color);
    a["email"] = st.email;
    a["synced"] = st.syncedAt != 0;
    a["error"] = st.error;
  }
  // Published calendars; the page has no provider field to store, it is only a hint there.
  JsonArray ics = doc["ics"].to<JsonArray>();
  for (size_t i = 0; i < s.ics.size(); i++) {
    const IcsStatus st = i < snap.ics.size() ? snap.ics[i] : IcsStatus();
    JsonObject c = ics.add<JsonObject>();
    c["label"] = s.ics[i].label;
    c["color"] = hex(s.ics[i].color);
    c["name"] = st.name;
    c["synced"] = st.synced;
    c["events"] = st.events;
    c["error"] = st.error;
  }
  doc["maxIcs"] = kMaxIcs;

  JsonObject d = doc["desk"].to<JsonObject>();
  d["on"] = s.desk;
  d["host"] = s.deskHost;
  d["user"] = s.deskUser;
  d["online"] = desk.online;
  d["loggedIn"] = desk.loggedIn;
  d["error"] = desk.error;

  doc["refresh"] = s.refreshMinutes;
  doc["updateUrl"] = s.updateUrl;
  doc["autoUpdate"] = s.autoUpdate;
  JsonObject o = doc["ota"].to<JsonObject>();
  o["state"] = otaStateName(ota.state);
  o["latest"] = ota.latest;
  o["progress"] = ota.progress;
  o["msg"] = ota.message;
  sendJson(doc);
}

// The page checks the PIN once when it unlocks; every change below checks it again.
void handlePin() {
  if (pinOk()) reply(kPinOk);
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
  reply(kAdded);
}

void handleRemoveAccount() {
  if (!pinOk()) return;
  Settings s = settings_get();
  const size_t i = server.arg("i").toInt();
  if (i < s.accounts.size()) {
    s.accounts.erase(s.accounts.begin() + i);
    settings_save(s);
  }
  reply(kRemoved);
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
  reply(kIcsAdded);
}

void handleRemoveIcs() {
  if (!pinOk()) return;
  Settings s = settings_get();
  const size_t i = server.arg("i").toInt();
  if (i < s.ics.size()) {
    s.ics.erase(s.ics.begin() + i);
    settings_save(s);
  }
  reply(kIcsRemoved);
}

// Sync interval in minutes (Settings > Sync on the display): minutes=2..60.
void handleSyncInterval() {
  if (!pinOk()) return;
  Settings s = settings_get();
  s.refreshMinutes = constrain((int)server.arg("minutes").toInt(), 1, 60);
  settings_save(s);
  reply(kSyncSaved);
}

void handleUpdateUrl() {
  if (!pinOk()) return;
  Settings s = settings_get();
  s.updateUrl = server.arg("url");
  s.updateUrl.trim();
  settings_save(s);
  reply(kLinkSaved);
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
  reply(kDeskSaved);
}

// "Test connection": tries the address and login as typed, before they are saved. An empty
// password means the saved one, as on the form. The page then polls the GET for the result.
void handleDeskTest() {
  if (!pinOk()) return;
  const Settings s = settings_get();
  String host = server.arg("host");
  host.trim();
  if (!host.length()) host = s.deskHost;
  String user = server.arg("user");
  user.trim();
  String pass = server.arg("pass");
  if (!pass.length() && user == s.deskUser) pass = s.deskPassword;
  desk_test(host, user, pass);
  reply(kTesting);
}

void handleDeskTestResult() {
  if (!pinOk()) return;
  const DeskTestResult r = desk_test_result();
  JsonDocument doc;
  doc["running"] = r.running;
  doc["done"] = r.done;
  doc["ok"] = r.ok;
  doc["message"] = r.message;
  sendJson(doc);
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
  server.on("/state", HTTP_GET, handleState);
  server.on("/pin", HTTP_POST, handlePin);
  server.on("/account/add", HTTP_POST, handleAddAccount);
  server.on("/account/remove", HTTP_POST, handleRemoveAccount);
  server.on("/ics/add", HTTP_POST, handleAddIcs);
  server.on("/ics/remove", HTTP_POST, handleRemoveIcs);
  server.on("/settings/sync", HTTP_POST, handleSyncInterval);
  server.on("/settings/update-url", HTTP_POST, handleUpdateUrl);
  server.on("/settings/desk", HTTP_POST, handleDesk);
  server.on("/settings/desk/test", HTTP_POST, handleDeskTest);
  server.on("/settings/desk/test", HTTP_GET, handleDeskTestResult);
  server.on("/update", HTTP_POST, handleUploadDone, handleUpload);
  // Test harness (tools/harness.py): one command, or a screenshot.
  server.on("/harness", HTTP_GET, [] {
    if (pinOk()) server.send(200, "text/plain", harness_run(server.arg("cmd")));
  });
  server.on("/shot.bmp", HTTP_GET, [] {
    if (pinOk()) harness_send_bmp(server);
  });
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
