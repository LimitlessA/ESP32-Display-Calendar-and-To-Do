#include <WiFi.h>
#include <algorithm>

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

bool tryConnect(const String &ssid, const String &password) {
  state = WifiState::Connecting;
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), password.c_str());
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < kConnectTimeoutMs) delay(200);
  return WiFi.status() == WL_CONNECTED;
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
    return true;
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
