#include "desk.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <deque>
#include <functional>

#include "psram.h"
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
constexpr const char *kNeedsLogin = "Needs a desk login (Features > Smart desk > Desk address and login)";
constexpr const char *kTestNoWifi = "The display is not on Wi-Fi";
constexpr const char *kTestNoHost = "Enter the desk address first";
constexpr const char *kTestHttp = "The desk answered with error %s";
constexpr const char *kTestGuest = "Desk found (%s cm). No login set: memory buttons and Stop work.";
constexpr const char *kTestLoginOk = "Desk found (%s cm). Login accepted.";
constexpr const char *kTestLoginBad = "Desk found, but it did not accept the login.";

enum class Cmd : uint8_t { Recall, HoldStop, Stop, TimerOn, TimerOff, TimerPause, TimerResume, AwayOn, AwayOff };
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
String loginKey;           // host + login the session belongs to; a change means a new session
volatile uint32_t timerApplyAt = 0;  // millis() of the last change of the timer lengths, 0 = none
DeskTestResult testResult;
volatile bool testBusy = false;
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
  JsonDocument doc(psram_json());
  doc["username"] = s.deskUser;
  doc["password"] = s.deskPassword;
  String body, out;
  serializeJson(doc, body);
  const int code = request(base + "/api/auth/login", "POST", body, out, false);
  JsonDocument reply(psram_json());
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
  JsonDocument doc(psram_json());
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
  JsonDocument timer(psram_json());
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

// A memory height from whatever shape the desk uses: 1200, {"height_mm":1200} or {"mm":1200}.
int memoryHeight(JsonVariantConst v) {
  if (v.is<JsonObjectConst>()) {
    for (const char *k : {"height_mm", "mm", "height", "position"})
      if (v[k].is<int>()) return v[k].as<int>();
    return 0;
  }
  return v.is<int>() ? v.as<int>() : 0;
}

void fetchMemory(const String &base, const Settings &s) {
  if (!s.deskUser.length()) return;
  if (!token.length() && !login(base, s)) return;
  String out;
  int code = request(base + "/api/desk/memory", "GET", "", out);
  if (code == 401 && login(base, s)) code = request(base + "/api/desk/memory", "GET", "", out);
  Serial.printf("[desk] GET /api/desk/memory -> %d %s\n", code, out.substring(0, 300).c_str());
  JsonDocument doc(psram_json());
  if (code != 200 || deserializeJson(doc, out)) return;
  // {"positions":{"M1":..}}, {"M1":..}, or a list of four.
  JsonVariantConst pos = doc["positions"].isNull() ? doc.as<JsonVariantConst>() : doc["positions"].as<JsonVariantConst>();
  update([&](DeskStatus &st) {
    for (int i = 0; i < 4; i++) {
      char key[3] = {'M', (char)('1' + i), 0};
      int mm = pos.is<JsonArrayConst>() ? memoryHeight(pos[i]) : memoryHeight(pos[key]);
      if (!mm && !pos.is<JsonArrayConst>()) mm = memoryHeight(pos[key + 1]);  // "1".."4"
      st.memoryMm[i] = mm;
    }
  });
}

// Recall memory M<slot> (1-4, as the desk's web UI numbers them). Public on the desk (guest
// mode), so no login is needed; with one, a refusal is retried signed in. Every request and
// reply goes to the serial log so a test on the real desk shows what it expects.
void recall(const String &base, const Settings &s, uint8_t slot) {
  String out;
  const String url = base + "/position";
  const String bodies[] = {"{\"position\":" + String(slot) + "}", "{\"position\":\"M" + String(slot) + "\"}"};
  for (const String &body : bodies) {
    out = "";
    int code = request(url, "POST", body, out, false);
    Serial.printf("[desk] POST /position %s -> %d %s\n", body.c_str(), code, out.substring(0, 200).c_str());
    if ((code == 401 || code == 403) && s.deskUser.length()) {
      out = "";
      code = authed(base, s, "/position", "POST", body);
      Serial.printf("[desk] signed-in retry -> %d\n", code);
    }
    const bool refused = out.indexOf("\"success\":false") >= 0 || out.indexOf("\"error\"") >= 0;
    if (code >= 200 && code < 300 && !refused) return;
  }
  update([](DeskStatus &st) { st.error = "The desk did not accept the memory button"; });
}

void send(const String &base, const Settings &s, const Command &c) {
  String out;
  switch (c.cmd) {
    case Cmd::Recall: recall(base, s, c.slot); break;
    case Cmd::Stop: request(base + "/api/desk/emergency-stop", "POST", "{}", out, false); break;
    case Cmd::HoldStop: authed(base, s, "/api/desk/hold/stop", "POST", "{}"); break;
    case Cmd::TimerOn: {
      // The lengths set on the display, else the ones saved on the desk, between the sit and stand memories.
      const DeskStatus st = desk_status();
      const int sitMin = s.deskSitMin ? s.deskSitMin : st.sitMinutes;
      const int standMin = s.deskStandMin ? s.deskStandMin : st.standMinutes;
      String body = "{";
      if (sitMin > 0 && standMin > 0) body += "\"sit_min\":" + String(sitMin) + ",\"stand_min\":" + String(standMin) + ",";
      const int sit = st.memoryMm[s.deskSitSlot - 1], stand = st.memoryMm[s.deskStandSlot - 1];
      if (sit > 0 && stand > 0) body += "\"sit_target_mm\":" + String(sit) + ",\"stand_target_mm\":" + String(stand) + ",";
      if (body.endsWith(",")) body.remove(body.length() - 1);
      authed(base, s, "/interval/start", "POST", body + "}");
      break;
    }
    case Cmd::TimerOff: authed(base, s, "/interval/stop", "POST", "{}"); break;
    case Cmd::TimerPause: authed(base, s, "/interval/pause", "POST", "{}"); break;
    case Cmd::TimerResume: authed(base, s, "/interval/resume", "POST", "{}"); break;
    case Cmd::AwayOn: authed(base, s, "/api/away/start", "POST", "{}"); break;
    case Cmd::AwayOff: authed(base, s, "/api/away/end", "POST", "{}"); break;
  }
  lastPoll = 0;  // show the result soon
}

void runTest();

void task(void *) {
  // A copy of the settings, taken again only when they change: copying them
  // (a dozen strings) 20 times a second churned the heap for nothing.
  Settings s;
  uint32_t seenSettings = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(50));
    runTest();
    if (settings_version() != seenSettings) {
      seenSettings = settings_version();
      s = settings_get();
      update([&](DeskStatus &st) { st.hasLogin = s.deskUser.length() > 0; });
      // New address or login (edited on the display or the setup page): start over with it.
      const String key = s.deskHost + "|" + s.deskUser + "|" + s.deskPassword;
      if (key != loginKey) {
        loginKey = key;
        token = "";
        ip = IPAddress();
        resolvedFor = "";
        lastMemory = 0;
        lastPoll = 0;
        update([&](DeskStatus &st) {
          st.loggedIn = false;
          st.error = "";
        });
      }
    }
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

    if (timerApplyAt && millis() - timerApplyAt > 1200) {
      timerApplyAt = 0;
      if (desk_status().timerRunning) send(base, s, {Cmd::TimerOn, 0});
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

// ---- Connection test ---------------------------------------------------------

struct TestJob {
  String host, user, password;
};

void finishTest(bool ok, const String &message) {
  xSemaphoreTake(lock, portMAX_DELAY);
  testResult.running = false;
  testResult.done = true;
  testResult.ok = ok;
  testResult.message = message;
  xSemaphoreGive(lock);
  testBusy = false;
}

TestJob *testJob = nullptr;  // waiting for the desk task, guarded by lock

// Runs a connection test asked for by desk_test(), on the desk task (a task
// of its own would take another 8 KB of internal RAM while it runs).
void runTest() {
  xSemaphoreTake(lock, portMAX_DELAY);
  TestJob *job = testJob;
  testJob = nullptr;
  xSemaphoreGive(lock);
  if (!job) return;
  String host = job->host;
  host.trim();
  String base, out;
  bool ok = false;
  String message;
  if (WiFi.status() != WL_CONNECTED) {
    message = kTestNoWifi;
  } else if (!host.length()) {
    message = kTestNoHost;
  } else {
    bool found = true;
    if (host.endsWith(".local")) {
      const IPAddress addr = MDNS.queryHost(host.substring(0, host.length() - 6), 2000);
      found = addr != IPAddress();
      base = "http://" + addr.toString();
    } else {
      base = "http://" + host;
    }
    JsonDocument doc(psram_json());
    const int code = found ? request(base + "/api/desk/status", "GET", "", out, false) : -1;
    if (!found || code <= 0) {
      message = strfmt(kNotFound, host.c_str());
    } else if (code != 200 || deserializeJson(doc, out)) {
      message = strfmt(kTestHttp, String(code).c_str());
    } else {
      const int mm = doc["height_mm"] | 0;
      const String cmText = String(mm / 10) + "." + String(mm % 10);
      if (!job->user.length()) {
        ok = true;
        message = strfmt(kTestGuest, cmText.c_str());
      } else {
        JsonDocument creds(psram_json());
        creds["username"] = job->user;
        creds["password"] = job->password;
        String body;
        serializeJson(creds, body);
        JsonDocument reply(psram_json());
        String got;
        const int lc = request(base + "/api/auth/login", "POST", body, got, false);
        String tok;
        if (lc == 200 && !deserializeJson(reply, got)) tok = reply["token"] | "";
        ok = tok.length() > 0;
        message = ok ? strfmt(kTestLoginOk, cmText.c_str()) : String(kTestLoginBad);
      }
    }
  }
  delete job;
  finishTest(ok, message);
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
  // Stack in PSRAM (no flash writes here): 8 KB, as HTTP, mDNS, JSON and the
  // connection test all run on it, at no cost in internal RAM.
  psram_task(task, "desk", 8192, nullptr, 1, nullptr, 0);
}

DeskStatus desk_status() {
  if (!lock) return DeskStatus();
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

void desk_timer_cmd(int what) {
  push(what == 1 ? Cmd::TimerOn : what == 2 ? Cmd::TimerPause : what == 3 ? Cmd::TimerResume : Cmd::TimerOff);
}

void desk_away(bool away) { push(away ? Cmd::AwayOn : Cmd::AwayOff); }

void desk_timer_apply() {
  uint32_t now = millis();
  timerApplyAt = now ? now : 1;
}

void desk_test(const String &host, const String &user, const String &password) {
  if (testBusy) return;
  testBusy = true;
  xSemaphoreTake(lock, portMAX_DELAY);
  testResult = DeskTestResult();
  testResult.running = true;
  delete testJob;
  testJob = new TestJob{host, user, password};
  xSemaphoreGive(lock);
}

DeskTestResult desk_test_result() {
  if (!lock) return DeskTestResult();
  xSemaphoreTake(lock, portMAX_DELAY);
  const DeskTestResult copy = testResult;
  xSemaphoreGive(lock);
  return copy;
}
