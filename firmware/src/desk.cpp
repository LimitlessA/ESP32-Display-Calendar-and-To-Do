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
constexpr uint32_t kHoldBeatMs = 350;      // the desk stops a hold 10 s (hold_timeout_ms) without a beat
constexpr uint32_t kMemoryEveryMs = 300000;  // memory heights change rarely
constexpr uint32_t kResolveEveryMs = 10000;  // look the .local name up again after failures
constexpr uint16_t kTimeoutMs = 1500;
// A login checks the password hash, writes the session to flash and wakes the desk:
// 0.6-1.6 s for a wrong password, longer for a right one, so 1.5 s often timed out.
constexpr uint16_t kLoginTimeoutMs = 8000;
constexpr const char *kNotFound = "Desk not found at %s";
constexpr const char *kLoginFailed = "The desk did not accept the login";
constexpr const char *kLoginNoAnswer = "The desk did not answer the login";
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
int deskSitMin = 0, deskStandMin = 0;  // the lengths saved on the desk, for when none are set on the display

// The sit/stand timer runs here, like the one on the desk's web page: it starts in the phase the
// desk is in now (no move) and at the end of each phase sends the desk to the other memory. The
// desk's own interval timer always starts by going to the sit memory, so it is not used.
struct LocalTimer {
  bool running = false, paused = false, stand = false;
  uint32_t start = 0, length = 0, elapsed = 0;  // ms; elapsed is kept while paused
} lt;
uint32_t lastPublish = 0;

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
      // The lookup often fails for minutes after a restart: use the address it had last time.
      // Kept in the settings, which the main loop writes (this task's stack is in PSRAM, so it
      // must not write flash itself).
      const String known = s.deskIp;
      if (ip != IPAddress()) {
        const String now = host + "|" + ip.toString();
        if (now != known) {
          Settings fresh = settings_get();
          fresh.deskIp = now;
          settings_save(fresh);
        }
      } else if (known.startsWith(host + "|")) {
        ip.fromString(known.substring(host.length() + 1));
      }
    }
    if (ip == IPAddress()) return false;
    url = "http://" + ip.toString();
  } else {
    url = "http://" + host;
  }
  return true;
}

// One request; returns the HTTP status (or a negative error) and the body.
int request(const String &url, const char *method, const String &body, String &out, bool auth = true,
            uint16_t timeoutMs = kTimeoutMs) {
  HTTPClient http;
  http.setTimeout(timeoutMs);
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
  const int code = request(base + "/api/auth/login", "POST", body, out, false, kLoginTimeoutMs);
  Serial.printf("[desk] login -> %d\n", code);
  JsonDocument reply(psram_json());
  if (code == 200 && !deserializeJson(reply, out)) token = reply["token"] | "";
  const bool ok = token.length() > 0;
  update([&](DeskStatus &st) {
    st.loggedIn = ok;
    if (!ok) st.error = code == 401 || code == 400 ? kLoginFailed : kLoginNoAnswer;
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
  update([&](DeskStatus &st) {
    if (!st.online) st.error = "";
    st.online = true;
    st.heightMm = height;
    st.moving = moving;
    st.state = state;
  });
}

int sitMinutes(const Settings &s) { return s.deskSitMin ? s.deskSitMin : deskSitMin > 0 ? deskSitMin : 30; }
int standMinutes(const Settings &s) { return s.deskStandMin ? s.deskStandMin : deskStandMin > 0 ? deskStandMin : 30; }

// Standing or sitting now: nearer the stand or the sit memory, else 1000 mm and up is standing.
bool standingNow(const Settings &s, const DeskStatus &st) {
  const int sit = st.memoryMm[s.deskSitSlot - 1], stand = st.memoryMm[s.deskStandSlot - 1];
  if (sit > 0 && stand > 0 && sit != stand) return stand > sit ? st.heightMm * 2 >= sit + stand : st.heightMm * 2 <= sit + stand;
  return st.heightMm >= 1000;
}

void startPhase(const Settings &s, bool stand) {
  lt.stand = stand;
  lt.length = 60000UL * (stand ? standMinutes(s) : sitMinutes(s));
  lt.start = millis();
  lt.elapsed = 0;
}

uint32_t timerElapsed() { return lt.paused ? lt.elapsed : millis() - lt.start; }

void publishTimer(const Settings &s) {
  const uint32_t left = lt.running ? lt.length - std::min(lt.length, timerElapsed()) : 0;
  update([&](DeskStatus &st) {
    st.timerRunning = lt.running;
    st.timerPaused = lt.paused;
    st.timerPhase = lt.running ? (lt.stand ? "STAND" : "SIT") : "IDLE";
    st.timerLeft = (left + 999) / 1000;
    st.sitMinutes = sitMinutes(s);
    st.standMinutes = standMinutes(s);
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
  JsonDocument timer(psram_json());
  if (request(base + "/interval/status", "GET", "", out, false) == 200 && !deserializeJson(timer, out)) {
    deskSitMin = (int)(timer["sit_minutes"] | 0.0f);
    deskStandMin = (int)(timer["stand_minutes"] | 0.0f);
  }
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
    // The desk's plain stop (public). Its emergency stop latches: every command after it
    // is refused until an admin clears it on the desk's web page.
    case Cmd::Stop: request(base + "/stop", "POST", "", out, false); break;
    case Cmd::HoldStop: {
      // A missed release would leave the desk moving until its 10 s hold timeout.
      const int code = authed(base, s, "/api/desk/hold/stop", "POST", "{}");
      if (code < 200 || code >= 300) request(base + "/stop", "POST", "", out, false);
      break;
    }
    case Cmd::TimerOn:
      // Starts in the position the desk is in now; no move. The desk's own timer, if it runs
      // (started on its web page), would send the desk to sit, so it is stopped.
      authed(base, s, "/interval/stop", "POST", "{}");
      lt.running = true;
      lt.paused = false;
      startPhase(s, standingNow(s, desk_status()));
      break;
    case Cmd::TimerOff: lt.running = lt.paused = false; break;
    case Cmd::TimerPause:
      if (lt.running && !lt.paused) {
        lt.elapsed = millis() - lt.start;
        lt.paused = true;
      }
      break;
    case Cmd::TimerResume:
      if (lt.running && lt.paused) {
        lt.start = millis() - lt.elapsed;
        lt.paused = false;
      }
      break;
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
      const int code = authed(base, s, "/api/desk/hold", "POST", dir > 0 ? "{\"direction\":\"up\"}" : "{\"direction\":\"down\"}");
      // The desk only ends a hold 10 s (hold_timeout_ms) after the last beat: if a beat
      // fails, stop now rather than leave the desk moving with the finger still down.
      if (code < 200 || code >= 300) {
        holding = 0;
        String out;
        request(base + "/stop", "POST", "", out, false);
      }
    }

    // New lengths apply to the phase that runs now, counted from its start.
    if (timerApplyAt && millis() - timerApplyAt > 1200) {
      timerApplyAt = 0;
      if (lt.running) lt.length = 60000UL * (lt.stand ? standMinutes(s) : sitMinutes(s));
      lastPublish = 0;
    }

    // End of a phase: to the other position from where the desk is now (it may have been moved by hand).
    if (lt.running && !lt.paused && millis() - lt.start >= lt.length) {
      const bool stand = !standingNow(s, desk_status());
      recall(base, s, stand ? s.deskStandSlot : s.deskSitSlot);
      startPhase(s, stand);
      lastPublish = 0;
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
    if (have) {
      send(base, s, c);
      lastPublish = 0;
    }
    if (!lastPublish || (lt.running && millis() - lastPublish >= 1000)) {
      lastPublish = millis();
      publishTimer(s);
    }

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

void desk_timer(bool on) { desk_timer_cmd(on ? 1 : 0); }

void desk_timer_cmd(int what) {
  // Show the result at once: the desk task may be busy with a request to the desk for a second
  // or two, and a button that does nothing for that long looks broken.
  const Settings s = settings_get();
  update([&](DeskStatus &st) {
    if (what == 1) {
      const bool stand = standingNow(s, st);
      st.timerRunning = true;
      st.timerPaused = false;
      st.timerPhase = stand ? "STAND" : "SIT";
      st.timerLeft = 60L * (stand ? standMinutes(s) : sitMinutes(s));
    } else if (what == 2) {
      st.timerPaused = true;
    } else if (what == 3) {
      st.timerPaused = false;
    } else {
      st.timerRunning = false;
      st.timerPaused = false;
    }
  });
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
