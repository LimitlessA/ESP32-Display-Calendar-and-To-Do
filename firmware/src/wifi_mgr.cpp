#include <WiFi.h>
#include <algorithm>
#include <esp_wifi.h>

#include "config.h"
#include "settings.h"
#include "wifi_mgr.h"

namespace {

constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kRetryMs = 30000;

SemaphoreHandle_t lock;  // guards the request fields
volatile WifiState state = WifiState::NotConfigured;

// A pending request from the Wi-Fi screen.
bool requestPending = false;
String requestSsid, requestPassword;

uint32_t lastAttempt = 0;

// Network watchdog (wifi_net_result()).
constexpr uint32_t kDeadAfterMs = 3 * 60 * 1000;      // no reply for this long, with failures...
constexpr uint8_t kDeadFailures = 3;                  // ...at least this many: reconnect
uint8_t kicks = 0;  // reconnects in a row without a reply in between
volatile uint32_t lastNetOk = 0;   // millis() of the last reply from any server
volatile uint8_t netFailures = 0;  // failed requests since then
uint32_t lastKick = 0;

bool tryConnect(const String &ssid, const String &password) {
  state = WifiState::Connecting;
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), password.c_str());
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < kConnectTimeoutMs) delay(200);
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!lastKick) lastNetOk = 0;  // a fresh connection starts the watchdog over; after a kick it keeps counting
  return true;
}

}  // namespace

void wifi_begin() {
  lock = xSemaphoreCreateMutex();
  // The login is kept in Settings; Wi-Fi itself does not save it to flash,
  // so connecting (from the sync task, whose stack is in PSRAM) never writes flash.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
}

bool wifi_service() {
  bool pending;
  String ssid, password;
  xSemaphoreTake(lock, portMAX_DELAY);
  pending = requestPending;
  if (pending) {
    ssid = requestSsid;
    password = requestPassword;
    requestPending = false;
  }
  xSemaphoreGive(lock);

  if (pending) {
    if (tryConnect(ssid, password)) {
      Settings s = settings_get();
      s.wifiSsid = ssid;
      s.wifiPassword = password;
      settings_save(s);
      state = WifiState::Connected;
    } else {
      state = WifiState::Failed;
      // Go back to the network that worked before, if any.
      const Settings s = settings_get();
      if (s.wifiSsid.length()) WiFi.begin(s.wifiSsid.c_str(), s.wifiPassword.c_str());
    }
    lastAttempt = millis();
    return WiFi.status() == WL_CONNECTED;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (state != WifiState::Failed) state = WifiState::Connected;
    const uint32_t now = millis();
    if (!lastNetOk) lastNetOk = now;
    const uint32_t silent = now - lastNetOk;
    if (netFailures >= kDeadFailures && silent > kDeadAfterMs && (!lastKick || now - lastKick > kDeadAfterMs)) {
      // First a plain reconnect; if that did not help, the radio is switched
      // off and on again as well (still no restart of the display).
      const bool hard = kicks++ > 0;
      Serial.printf("[wifi] connected, but %u requests failed in %lu s: %s\n", netFailures,
                    (unsigned long)(silent / 1000), hard ? "radio off and on" : "reconnect");
      lastKick = now;
      netFailures = 0;
      WiFi.disconnect();
      if (hard) {
        WiFi.mode(WIFI_OFF);
        delay(500);
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(HOSTNAME);
      }
      lastAttempt = 0;  // tryConnect() below, at once
    } else {
      return true;
    }
  }

  const Settings s = settings_get();
  if (!s.wifiSsid.length()) {
    state = WifiState::NotConfigured;
    return false;
  }
  if (state == WifiState::Connected) state = WifiState::Disconnected;
  if (lastAttempt == 0 || millis() - lastAttempt > kRetryMs) {
    lastAttempt = millis();
    if (tryConnect(s.wifiSsid, s.wifiPassword)) {
      state = WifiState::Connected;
      return true;
    }
    state = WifiState::Disconnected;
  }
  return false;
}

void wifi_net_result(bool ok) {
  if (ok) {
    lastNetOk = millis();
    netFailures = 0;
    lastKick = 0;
    kicks = 0;
  } else if (netFailures < 255) {
    netFailures++;
  }
}

void wifi_sleep(bool sleep) {
  if (sleep) {
    esp_wifi_stop();  // the driver and its settings stay in RAM
    return;
  }
  esp_wifi_start();
  lastNetOk = 0;  // asleep is not silent: start the watchdog over
  lastKick = 0;
  kicks = 0;
  netFailures = 0;
  lastAttempt = 0;  // reconnect at the next wifi_service(), not 30 s later
}

WifiState wifi_state() {
  return state;
}

String wifi_ssid() {
  return WiFi.status() == WL_CONNECTED ? WiFi.SSID() : settings_get().wifiSsid;
}

int wifi_rssi() {
  return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -127;
}

String wifi_ip() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
}

void wifi_scan_start() {
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
}

bool wifi_scan_results(std::vector<WifiNetwork> &out) {
  const int n = WiFi.scanComplete();
  if (n < 0) return false;
  out.clear();
  for (int i = 0; i < n; i++) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;  // hidden network
    auto dup = std::find_if(out.begin(), out.end(), [&](const WifiNetwork &w) { return w.ssid == ssid; });
    if (dup != out.end()) {
      dup->rssi = max(dup->rssi, (int)WiFi.RSSI(i));  // same name on 2.4 and 5 GHz
      continue;
    }
    out.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
  }
  std::sort(out.begin(), out.end(), [](const WifiNetwork &a, const WifiNetwork &b) { return a.rssi > b.rssi; });
  return true;
}

void wifi_connect(const String &ssid, const String &password) {
  xSemaphoreTake(lock, portMAX_DELAY);
  requestSsid = ssid;
  requestPassword = password;
  requestPending = true;
  xSemaphoreGive(lock);
  state = WifiState::Connecting;
}
