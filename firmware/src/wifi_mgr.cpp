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

// The last connection events, newest last, for the setup page and Serial:
// why the link dropped (the access point's reason code), IP lost/got, and
// what the watchdog did. Written from the Wi-Fi event task too, hence the lock.
constexpr uint8_t kLogLines = 10;
char logLines[kLogLines][72];
uint8_t logNext = 0, logCount = 0;
portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

void note(const char *fmt, ...) {
  char text[56];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
  char stamp[16];
  const time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(stamp, sizeof(stamp), "%02d %02d:%02d:%02d", tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);  // not strftime: 13 KB
  } else {
    snprintf(stamp, sizeof(stamp), "+%lus", (unsigned long)(millis() / 1000));
  }
  Serial.printf("[wifi] %s\n", text);
  portENTER_CRITICAL(&logMux);
  snprintf(logLines[logNext], sizeof(logLines[0]), "%s %s", stamp, text);
  logNext = (logNext + 1) % kLogLines;
  if (logCount < kLogLines) logCount++;
  portEXIT_CRITICAL(&logMux);
}

// The common reasons, by name (WiFi.disconnectReasonName() costs ~6 KB of flash).
const char *reasonName(uint8_t r) {
  switch (r) {
    case 2: return "auth expired";
    case 3: return "deauth by router";
    case 4: return "inactive";
    case 6: case 7: return "not authenticated";
    case 8: return "left";
    case 15: case 204: return "handshake timeout";
    case 200: return "beacon timeout";
    case 201: return "no AP found";
    case 202: return "auth failed";
    case 203: return "assoc failed";
    case 205: return "connection failed";
    default: return "other";
  }
}

void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      note("joined, channel %u", info.wifi_sta_connected.channel);
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      note("link dropped, reason %u (%s), %d dBm", info.wifi_sta_disconnected.reason,
           reasonName(info.wifi_sta_disconnected.reason),
           info.wifi_sta_disconnected.rssi);
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      note("got IP %s", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      note("lost IP address");
      break;
    default:
      break;
  }
}

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
  WiFi.onEvent(onEvent);
}

String wifi_log() {
  String out;
  portENTER_CRITICAL(&logMux);
  char copy[kLogLines][72];
  const uint8_t n = logCount, first = (logNext + kLogLines - logCount) % kLogLines;
  for (uint8_t i = 0; i < n; i++) memcpy(copy[i], logLines[(first + i) % kLogLines], sizeof(copy[0]));
  portEXIT_CRITICAL(&logMux);
  for (uint8_t i = 0; i < n; i++) {
    out += copy[i];
    out += '\n';
  }
  return out;
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
      note("stuck: %u failed, %lu s silent, %d dBm: %s", netFailures, (unsigned long)(silent / 1000),
           (int)WiFi.RSSI(), hard ? "radio off/on" : "reconnect");
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
