#include "desk.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <deque>
#include <functional>

#include "settings.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr uint32_t kPollMs = 2000;         // status while the desk stands still
constexpr uint32_t kPollMovingMs = 500;    // ...and while it moves
constexpr uint32_t kHoldBeatMs = 350;      // the desk stops a hold after ~500 ms without a beat
constexpr uint32_t kMemoryEveryMs = 300000;  // memory heights change rarely
constexpr uint32_t kResolveEveryMs = 60000;  // look the .local name up again after failures
constexpr uint16_t kTimeoutMs = 1500;
constexpr const char *kNotFound = "Desk not found at %s";
constexpr const char *kLoginFailed = "The desk did not accept the login";
constexpr const char *kNeedsLogin = "Needs a desk login on the setup page";

enum class Cmd : uint8_t { Recall, HoldStop, Stop, TimerOn, TimerOff, AwayOn, AwayOff };
struct Command {
  Cmd cmd;
  uint8_t slot;
};

// ---- State -------------------------------------------------------------------

SemaphoreHandle_t lock;
DeskStatus status;
std::deque<Command> queue;
volatile int holding = 0;  // +1 / -1 while a button is held
String token;              // desk session; desks forget sessions when they restart
IPAddress ip;
String resolvedFor;
uint32_t lastResolve = 0, lastPoll = 0, lastMemory = 0, lastBeat = 0;

// ---- Code --------------------------------------------------------------------

String strfmt(const char *fmt, const char *arg) {
  char buf[96];
  snprintf(buf, sizeof(buf), fmt, arg);
  return buf;
}

void update(const std::function<void(DeskStatus &)> &fn) {
  xSemaphoreTake(lock, portMAX_DELAY);
  fn(status);
  status.version++;
  xSemaphoreGive(lock);
}

// "smartdesk-a43c.local" needs mDNS; anything else is an IP or a DNS name.
bool baseUrl(const Settings &s, String &url) {
  String host = s.deskHost;
  host.trim();
  if (!host.length()) return false;
  if (host.endsWith(".local")) {
    if (resolvedFor != host || (ip == IPAddress() && millis() - lastResolve > kResolveEveryMs)) {
      lastResolve = millis();
      resolvedFor = host;
      ip = MDNS.queryHost(host.substring(0, host.length() - 6), 2000);
    }
    if (ip == IPAddress()) return false;
    url = "http://" + ip.toString();
  } else {
    url = "http://" + host;
  }
  return true;
}

// One request; returns the HTTP status (or a negative error) and the body.
int request(const String &url, const char *method, const String &body, String &out, bool auth = true) {
  HTTPClient http;
  http.setTimeout(kTimeoutMs);
  http.setConnectTimeout(kTimeoutMs);
  if (!http.begin(url)) return -1;
  if (auth && token.length()) http.addHeader("Authorization", "Bearer " + token);
  int code;
  if (strcmp(method, "GET") == 0) {
    code = http.GET();
  } else {
    http.addHeader("Content-Type", "application/json");
    code = http.sendRequest(method, body);
  }
  if (code > 0) out = http.getString();
  http.end();
  return code;
}

bool login(const String &base, const Settings &s) {
  token = "";
  if (!s.deskUser.length()) return false;
  JsonDocument doc;
  doc["username"] = s.deskUser;
  doc["password"] = s.deskPassword;
  String body, out;
  serializeJson(doc, body);
  const int code = request(base + "/api/auth/login", "POST", body, out, false);
  JsonDocument reply;
  if (code == 200 && !deserializeJson(reply, out)) token = reply["token"] | "";
  const bool ok = token.length() > 0;
  update([&](DeskStatus &st) {
    st.loggedIn = ok;
    if (!ok) st.error = kLoginFailed;
  });
  return ok;
}

// Sends a request that needs the login, logging in first or again if needed.
int authed(const String &base, const Settings &s, const String &path, const char *method, const String &body) {
  if (!s.deskUser.length()) {
    update([](DeskStatus &st) { st.error = kNeedsLogin; });
    return 401;
  }
  if (!token.length() && !login(base, s)) return 401;
  String out;
  int code = request(base + path, method, body, out);
  if (code == 401 && login(base, s)) code = request(base + path, method, body, out);
  return code;
}

void poll(const String &base) {
  String out;
  JsonDocument doc;
  if (request(base + "/api/desk/status", "GET", "", out, false) != 200 || deserializeJson(doc, out)) {
    ip = IPAddress();  // look the name up again next time
    update([&](DeskStatus &st) {
      st.online = false;
      st.error = strfmt(kNotFound, resolvedFor.c_str());
    });
    return;
  }
  const int height = doc["height_mm"] | 0;
  const bool moving = doc["is_moving"] | false;
  const String state = doc["state"] | "";
  JsonDocument timer;
  const bool haveTimer = request(base + "/interval/status", "GET", "", out, false) == 200 && !deserializeJson(timer, out);
  update([&](DeskStatus &st) {
    if (!st.online) st.error = "";
    st.online = true;
    st.heightMm = height;
    st.moving = moving;
    st.state = state;
    if (haveTimer) {
      st.timerRunning = timer["running"] | false;
      st.timerPaused = timer["paused"] | false;
      st.timerPhase = timer["phase"] | "";
      st.timerLeft = timer["remaining_seconds"] | 0L;
      st.sitMinutes = (int)(timer["sit_minutes"] | 0.0f);
      st.standMinutes = (int)(timer["stand_minutes"] | 0.0f);
    }
  });
}

void fetchMemory(const String &base, const Settings &s) {
  if (!s.deskUser.length()) return;
  if (!token.length() && !login(base, s)) return;
  String out;
  int code = request(base + "/api/desk/memory", "GET", "", out);
  if (code == 401 && login(base, s)) code = request(base + "/api/desk/memory", "GET", "", out);
  JsonDocument doc;
  if (code != 200 || deserializeJson(doc, out)) return;
  update([&](DeskStatus &st) {
    const char *keys[] = {"M1", "M2", "M3", "M4"};
    for (int i = 0; i < 4; i++) st.memoryMm[i] = doc["positions"][keys[i]] | 0;
  });
}

void send(const String &base, const Settings &s, const Command &c) {
  String out;
  switch (c.cmd) {
    case Cmd::Recall: {
      // Public on the desk, like its guest mode.
      request(base + "/position", "POST", "{\"position\":" + String(c.slot) + "}", out, false);
      break;
    }
    case Cmd::Stop: request(base + "/api/desk/emergency-stop", "POST", "{}", out, false); break;
    case Cmd::HoldStop: authed(base, s, "/api/desk/hold/stop", "POST", "{}"); break;
    case Cmd::TimerOn: {
      // Uses the timer lengths saved on the desk, between the sit and stand memories.
      const DeskStatus st = desk_status();
      String body = "{";
      if (st.sitMinutes > 0) body += "\"sit_min\":" + String(st.sitMinutes) + ",\"stand_min\":" + String(st.standMinutes) + ",";
      const int sit = st.memoryMm[s.deskSitSlot - 1], stand = st.memoryMm[s.deskStandSlot - 1];
      if (sit > 0 && stand > 0) body += "\"sit_target_mm\":" + String(sit) + ",\"stand_target_mm\":" + String(stand) + ",";
      if (body.endsWith(",")) body.remove(body.length() - 1);
      authed(base, s, "/interval/start", "POST", body + "}");
      break;
    }
    case Cmd::TimerOff: authed(base, s, "/interval/stop", "POST", "{}"); break;
    case Cmd::AwayOn: authed(base, s, "/api/away/start", "POST", "{}"); break;
    case Cmd::AwayOff: authed(base, s, "/api/away/end", "POST", "{}"); break;
  }
  lastPoll = 0;  // show the result soon
}

void task(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(50));
    const Settings s = settings_get();
    update([&](DeskStatus &st) { st.hasLogin = s.deskUser.length() > 0; });
    String base;
    if (!s.desk || WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (!baseUrl(s, base)) {
      update([&](DeskStatus &st) {
        st.online = false;
        st.error = strfmt(kNotFound, s.deskHost.c_str());
      });
      vTaskDelay(pdMS_TO_TICKS(kPollMs));
      continue;
    }

    // Held raise/lower: a beat keeps it going, the desk stops by itself without one.
    const int dir = holding;
    if (dir && millis() - lastBeat >= kHoldBeatMs) {
      lastBeat = millis();
      authed(base, s, "/api/desk/hold", "POST", dir > 0 ? "{\"direction\":\"up\"}" : "{\"direction\":\"down\"}");
    }

    bool have = false;
    Command c;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!queue.empty()) {
      c = queue.front();
      queue.pop_front();
      have = true;
    }
    xSemaphoreGive(lock);
    if (have) send(base, s, c);

    const DeskStatus st = desk_status();
    const uint32_t every = st.moving || dir ? kPollMovingMs : kPollMs;
    if (millis() - lastPoll >= every || !lastPoll) {
      lastPoll = millis();
      poll(base);
    }
    if (st.online && (!lastMemory || millis() - lastMemory > kMemoryEveryMs)) {
      lastMemory = millis();
      fetchMemory(base, s);
    }
  }
}

void push(Cmd cmd, uint8_t slot = 0) {
  xSemaphoreTake(lock, portMAX_DELAY);
  // A newer Stop wins over everything still waiting.
  if (cmd == Cmd::Stop) queue.clear();
  queue.push_back({cmd, slot});
  xSemaphoreGive(lock);
}

}  // namespace

void desk_begin() {
  lock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(task, "desk", 6144, nullptr, 1, nullptr, 0);
}

DeskStatus desk_status() {
  xSemaphoreTake(lock, portMAX_DELAY);
  const DeskStatus copy = status;
  xSemaphoreGive(lock);
  return copy;
}

void desk_recall(uint8_t slot) {
  if (slot >= 1 && slot <= 4) push(Cmd::Recall, slot);
}

void desk_hold(int direction) {
  const int was = holding;
  holding = direction;
  lastBeat = 0;
  if (!direction && was) push(Cmd::HoldStop);
}

void desk_stop() {
  holding = 0;
  push(Cmd::Stop);
}

void desk_timer(bool on) { push(on ? Cmd::TimerOn : Cmd::TimerOff); }

void desk_away(bool away) { push(away ? Cmd::AwayOn : Cmd::AwayOff); }
