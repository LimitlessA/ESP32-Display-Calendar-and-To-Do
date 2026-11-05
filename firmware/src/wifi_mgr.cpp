#include <WiFi.h>
#include <WiFiUdp.h>
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

// What the screen shows while Wi-Fi is down (wifi_problem()).
volatile uint32_t downSinceMs = 0;  // millis() when the trouble began, 0 = fine
volatile time_t downSince = 0;      // the same as a clock time, if the clock was set
volatile uint8_t lastReason = 0;    // access point's reason code of the last drop
volatile int8_t lastDropRssi = 0;
volatile bool stuck = false;        // joined, but the router does not answer

void markDown() {
  if (downSinceMs) return;
  downSinceMs = millis() | 1;
  const time_t now = time(nullptr);
  downSince = now > 1700000000 ? now : 0;
}

void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      note("joined, channel %u", info.wifi_sta_connected.channel);
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      lastReason = info.wifi_sta_disconnected.reason;
      lastDropRssi = info.wifi_sta_disconnected.rssi;
      markDown();
      // Internal RAM in KB too: Wi-Fi can't join without it (2026-11-05).
      note("link dropped, reason %u (%s), %d dBm, %uK", info.wifi_sta_disconnected.reason,
           reasonName(info.wifi_sta_disconnected.reason), info.wifi_sta_disconnected.rssi,
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      if (!stuck) downSinceMs = 0;  // a stuck link is fine only once the router answers again
      note("got IP %s, %uK", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      note("lost IP address");
      break;
    default:
      break;
  }
}

// Network watchdog. Wi-Fi can stay "connected" while nothing gets through
// (2026-11-03 and 2026-11-05 on the 7": no DNS, no ARP, the setup page
// down). So once a minute, unless a server answered just before, the display
// asks the router (its DNS server) for a name over UDP. Three unanswered
// checks in a row (about a minute, they are 20 s apart once one fails) mean
// the link is stuck: it is dropped and joined again, which also fetches new
// keys from the router; if that did not help, the radio is switched off and on
// as well. Never a restart (Andrei does not want those).
constexpr uint32_t kProbeEveryMs = 60 * 1000;
constexpr uint32_t kProbeRetryMs = 20 * 1000;
constexpr uint8_t kProbeFailures = 3;
constexpr uint32_t kKickAgainMs = 2 * 60 * 1000;  // after a kick, the next one no sooner than this
uint8_t kicks = 0;  // reconnects in a row without a reply in between
volatile uint32_t lastNetOk = 0;   // millis() of the last reply from any server
uint8_t probeFails = 0;
uint32_t lastProbe = 0;
uint32_t lastKick = 0;
uint16_t probeId = 0;

// One DNS question to the router; true if it answered (any answer will do).
bool probeRouter() {
  IPAddress dns = WiFi.dnsIP(0);
  if (dns == IPAddress((uint32_t)0)) dns = WiFi.gatewayIP();
  WiFiUDP udp;
  if (!udp.begin(0)) return false;
  // Header (id, recursion desired, 1 question), then "pool.ntp.org" A IN.
  static const uint8_t kQuestion[] = {4, 'p', 'o', 'o', 'l', 3, 'n', 't', 'p', 3, 'o', 'r', 'g', 0, 0, 1, 0, 1};
  const uint16_t id = ++probeId;
  uint8_t q[12 + sizeof(kQuestion)] = {(uint8_t)(id >> 8), (uint8_t)id, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0};
  memcpy(q + 12, kQuestion, sizeof(kQuestion));
  bool ok = false;
  for (int attempt = 0; attempt < 2 && !ok; attempt++) {
    if (!udp.beginPacket(dns, 53)) break;
    udp.write(q, sizeof(q));
    if (!udp.endPacket()) break;
    const uint32_t t0 = millis();
    while (!ok && millis() - t0 < 1500) {
      if (udp.parsePacket() >= 12) {
        uint8_t h[2];
        ok = udp.read(h, 2) == 2 && h[0] == q[0] && h[1] == q[1];
        udp.flush();
      } else {
        delay(20);
      }
    }
  }
  udp.stop();
  return ok;
}

bool tryConnect(const String &ssid, const String &password) {
  state = WifiState::Connecting;
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), password.c_str());
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < kConnectTimeoutMs) delay(200);
  if (WiFi.status() != WL_CONNECTED) return false;
  probeFails = 0;
  lastProbe = millis() - kProbeEveryMs + 10000;  // check the new link 10 s from now
  if (!stuck) downSinceMs = 0;
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
    const uint32_t every = probeFails ? kProbeRetryMs : kProbeEveryMs;
    // A server answered lately: the link works, no need to ask the router.
    const bool recentOk = lastNetOk && now - lastNetOk < every;
    if (now - lastProbe < every || recentOk) {
      if (recentOk) lastProbe = now;
      return !stuck;
    }
    lastProbe = now;
    if (probeRouter()) {
      if (stuck || probeFails) note("router answers again, %d dBm", (int)WiFi.RSSI());
      wifi_net_result(true);
      return true;
    }
    probeFails++;
    note("router did not answer (%u), %d dBm", probeFails, (int)WiFi.RSSI());
    if (probeFails < kProbeFailures) return !stuck;
    if (!stuck) {
      stuck = true;
      markDown();
      lastReason = 0;
      lastDropRssi = (int8_t)WiFi.RSSI();
    }
    if (lastKick && now - lastKick < kKickAgainMs) return false;
    // First a plain reconnect; if that did not help, the radio is switched
    // off and on again as well (still no restart of the display).
    const bool hard = kicks++ > 0;
    note("stuck, %d dBm: %s", (int)WiFi.RSSI(), hard ? "radio off/on" : "reconnect");
    lastKick = now;
    WiFi.disconnect();
    if (hard) {
      WiFi.mode(WIFI_OFF);
      delay(500);
      WiFi.mode(WIFI_STA);
      WiFi.setHostname(HOSTNAME);
    }
    lastAttempt = 0;  // tryConnect() below, at once
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
    lastNetOk = millis() | 1;
    probeFails = 0;
    lastKick = 0;
    kicks = 0;
    stuck = false;
    if (WiFi.status() == WL_CONNECTED) downSinceMs = 0;
  } else if (!probeFails) {
    lastProbe = 0;  // a request failed: check the router at once
  }
}

bool wifi_problem(String &brief, String &detail) {
  const Settings s = settings_get();
  if (!s.wifiSsid.length() || state == WifiState::Connecting) return false;
  const uint32_t since = downSinceMs;
  const bool down = WiFi.status() != WL_CONNECTED;
  if (!since || (!down && !stuck)) return false;
  const uint32_t mins = (millis() - since) / 60000;
  char at[8] = "";
  const time_t t = downSince;
  if (t) {
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(at, sizeof(at), "%02d:%02d", tm.tm_hour, tm.tm_min);
  }
  const char *what = stuck && !down ? "No answer from router" : "Wi-Fi down";
  brief = t ? String(what) + " " + at : String(what);
  char text[200];
  if (stuck && !down) {
    snprintf(text, sizeof(text),
             "%s for %lu min (since %s). Wi-Fi is joined, %d dBm, but the router does not answer. "
             "The display reconnects by itself.",
             what, (unsigned long)mins, t ? at : "start", (int)WiFi.RSSI());
  } else {
    snprintf(text, sizeof(text), "%s for %lu min (since %s). Last drop: reason %u (%s), %d dBm. Retrying every 30 s.",
             what, (unsigned long)mins, t ? at : "start", lastReason, lastReason ? reasonName(lastReason) : "none",
             (int)lastDropRssi);
  }
  detail = text;
  return true;
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
  probeFails = 0;
  stuck = false;
  downSinceMs = 0;
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
