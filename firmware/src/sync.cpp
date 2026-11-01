#include <Arduino.h>
#include <stdarg.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <deque>
#include <map>
#include <memory>

#include "google_root_cas.h"
#include "settings.h"
#include "storage.h"
#include "sync.h"
#include "wifi_mgr.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

// Error texts shown on the display
constexpr const char *kErrBadUrl = "bad URL";
constexpr const char *kErrHttp = "HTTP %d";
constexpr const char *kErrBadReply = "bad reply: %s";
constexpr const char *kErrAccount = "%s: %s";  // account name, error
constexpr const char *kErrJoin = "; ";
constexpr const char *kErrAction = "%s failed: %s";  // action name, error
constexpr const char *kErrNoWifiSetup = "Wi-Fi is not set up";
constexpr const char *kErrNoWifi = "No Wi-Fi connection";
constexpr const char *kActDismiss = "Dismiss";
constexpr const char *kActComplete = "Complete";
constexpr const char *kActUndo = "Undo";
constexpr const char *kActCalendar = "Calendar change";
constexpr const char *kActAdd = "Add task";
constexpr const char *kActDue = "Move task";
constexpr const char *kActStar = "Countdown";
constexpr const char *kActOther = "Action";

constexpr const char *kCachePath = "/calendar/cache_%08lx.json";  // offline copy on the SD card, per account list
constexpr const char *kNtpServer1 = "time.google.com";
constexpr const char *kNtpServer2 = "pool.ntp.org";
constexpr uint32_t kConnectTimeoutMs = 10000;
constexpr uint32_t kReplyTimeoutMs = 25000;  // Apps Script can take a while on a cold start
constexpr uint32_t kKeepAliveMs = 30000;     // reuse a TLS connection only this long after its last reply
constexpr int kMaxHops = 4;                  // redirects followed per request
constexpr uint32_t kIdleLoopMs = 1000;       // how often the worker wakes when online
constexpr uint32_t kOfflineLoopMs = 5000;    // ...and when offline
constexpr uint32_t kTaskStack = 16384;
constexpr uint8_t kActionTries = 3;  // a failed connection is retried before it counts as an error
constexpr const char *kLocalPrefix = "new:";  // id of a task added here that Google has not answered for yet
constexpr time_t kClockValid = 1700000000;   // the clock is set once it is past this (2023)
// Feed format 2: short keys, no empty fields, calendar index instead of name
// and color, the window the feed covers, and "same" when nothing changed.
constexpr const char *kApiVersion = "v=2";
constexpr const char *kErrRedirects = "too many redirects";

// ---- State -------------------------------------------------------------------

enum class ActionType { Dismiss, Complete, Uncomplete, ShowCalendar, AddTask, SetDue, Star };

struct Action {
  ActionType type;
  uint8_t account;
  String hiddenKey;  // entry in `hidden` this action covers, if any
  String query;      // extra query string for the Apps Script call
  // Task actions: list=...&task=... is added when the action is sent, so a
  // task added on the display can be completed or moved before Google has
  // given it a real id. For AddTask, taskId is the temporary "new:" id.
  String taskId;
  String listId;
  uint8_t tries = 0;
};

SemaphoreHandle_t lock;
TaskHandle_t worker;
Snapshot snapshot;
std::deque<Action> pending;
// Items removed on screen but not yet confirmed by a fresh sync. Value is
// true once the action has been sent, so the next sync can drop the entry.
std::map<String, bool> hidden;
// Tasks added on the display: temporary "new:" id -> Google's task id and list id.
std::map<String, std::pair<String, String>> addedIds;
volatile bool refreshRequested = true;
volatile bool fullRequested = false;  // ask for the whole feed even if it looks unchanged
volatile time_t weekRequested = 0;

// Kept-open HTTPS connections, one per host, used only by the worker task.
struct Conn {
  String host;
  WiFiClientSecure client;
  HTTPClient http;  // kept too: deleting an HTTPClient closes its connection
  uint32_t usedAt = 0;
};
std::unique_ptr<Conn> conns[2];
String timezone;  // POSIX TZ built from the feed's UTC offset

// ---- Code --------------------------------------------------------------------

String strf(const char *fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  return buf;
}

struct Guard {
  Guard() { xSemaphoreTake(lock, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(lock); }
};

String eventHiddenKey(uint8_t account, const String &key) { return "e" + String(account) + ":" + key; }
String taskHiddenKey(uint8_t account, const String &id) { return "t" + String(account) + ":" + id; }

String urlEncode(const String &s) {
  static const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    const uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

String cachePath(const Account &a) {
  uint32_t h = 5381;
  for (size_t i = 0; i < a.url.length(); i++) h = h * 33 + a.url[i];
  char buf[40];
  snprintf(buf, sizeof(buf), kCachePath, (unsigned long)h);
  return buf;
}

String accountUrl(const Account &a, const String &query) {
  String url = a.url + "?token=" + urlEncode(a.token);
  if (query.length()) url += "&" + query;
  return url;
}

String hostOf(const String &url) {
  if (!url.startsWith("https://")) return "";
  const int from = 8;
  int to = from;
  while (to < (int)url.length() && url[to] != '/' && url[to] != '?' && url[to] != ':') to++;
  return url.substring(from, to);
}

// The connection for this host: the open one if there is one, else the
// least recently used slot. Connections idle too long are closed first, as
// Google may already have dropped them.
Conn &connFor(const String &host) {
  Conn *pick = nullptr;
  for (auto &c : conns) {
    if (c && c->host == host) pick = c.get();
  }
  for (auto &c : conns) {
    if (pick) break;
    if (!c) {
      c.reset(new Conn());
      c->client.setCACert(GOOGLE_ROOT_CAS);
      pick = c.get();
    }
  }
  if (!pick) pick = conns[0]->usedAt <= conns[1]->usedAt ? conns[0].get() : conns[1].get();
  if (pick->host != host || millis() - pick->usedAt > kKeepAliveMs) {
    pick->client.stop();
    pick->host = host;
  }
  return *pick;
}

void closeIdleConnections() {
  for (auto &c : conns) {
    if (c && c->usedAt && millis() - c->usedAt > kKeepAliveMs) {
      c->client.stop();
      c->usedAt = 0;
    }
  }
}

// One GET on a kept-open connection. Returns the HTTP code (negative:
// HTTPClient error). The whole reply is read, also for redirects, so the
// connection can carry the next request.
int getOnce(Conn &c, const String &url, String &body, String &location, String &err) {
  HTTPClient &http = c.http;
  http.setReuse(true);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kReplyTimeoutMs);
  if (!http.begin(c.client, url)) {
    err = kErrBadUrl;
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }
  static const char *kHeaders[] = {"Location"};
  http.collectHeaders(kHeaders, 1);
  const int code = http.GET();
  c.usedAt = millis();
  if (code > 0) {
    String reply = http.getString();
    if (code == HTTP_CODE_OK) {
      if (!reply.length()) c.client.stop();  // cut short: don't reuse
      body = std::move(reply);
    } else {
      location = http.header("Location");
    }
  } else {
    err = HTTPClient::errorToString(code);
    // HTTPClient says "connection refused" for any failed connect, a failed
    // secure handshake (e.g. short on memory) included: add the TLS reason.
    char tls[96];
    if (code == HTTPC_ERROR_CONNECTION_REFUSED && c.client.lastError(tls, sizeof(tls))) {
      err += String(" (") + tls + ")";
    }
    c.client.stop();
  }
  http.end();
  return code;
}

// Apps Script answers on script.google.com with a redirect to
// script.googleusercontent.com. Both connections stay open between requests,
// so a sync of several accounts, the week and any taps costs two TLS
// handshakes instead of two per request.
bool httpGet(const String &url, String &body, String &err, int *status = nullptr) {
  String next = url;
  for (int hop = 0; hop < kMaxHops; hop++) {
    const String host = hostOf(next);
    if (!host.length()) {
      err = kErrBadUrl;
      return false;
    }
    String location;
    int code = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
      Conn &c = connFor(host);
      const bool reused = c.client.connected();
      location = "";
      err = "";
      code = getOnce(c, next, body, location, err);
      // Google may close a kept-open connection at any time: retry once on a new one.
      if (code > 0 || !reused) break;
    }
    if (status) *status = code;
    if (code == HTTP_CODE_OK) return true;
    if (code >= 300 && code < 400 && location.length()) {
      if (location.startsWith("/")) {
        next = String("https://") + host + location;
      } else {
        next = location;
      }
      continue;
    }
    if (code > 0) err = strf(kErrHttp, code);
    return false;
  }
  err = kErrRedirects;
  return false;
}

bool parseResponse(const String &body, JsonDocument &doc, String &err) {
  const DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    // Usually a Google error page (e.g. an old or wrongly shared deployment):
    // show its title so the reason is visible on the display.
    String hint = body;
    const int t = body.indexOf("<title>");
    if (t >= 0) {
      const int e = body.indexOf("</title>", t);
      hint = body.substring(t + 7, e > t ? e : t + 7 + 60);
    }
    hint = hint.substring(0, 60);
    hint.trim();
    err = strf(kErrBadReply, jerr.c_str());
    if (hint.length()) err += " (" + hint + ")";
    return false;
  }
  if (doc["error"].is<const char *>()) {
    err = doc["error"].as<String>();
    return false;
  }
  return true;
}

uint32_t parseColor(const char *s) {
  if (!s || s[0] != '#') return 0x8B94A0;
  return strtoul(s + 1, nullptr, 16);
}

// What an event's calendar index ("c") points at. Published (ICS) calendars
// take their name and color from the display's settings.
struct CalRef {
  String name;
  uint32_t color;
};

std::vector<CalRef> calRefs(JsonArray arr, const Settings &s) {
  std::vector<CalRef> out;
  for (JsonObject c : arr) {
    const int ics = c["ics"] | 0;
    if (ics > 0 && ics <= (int)s.ics.size() && s.ics[ics - 1].label.length()) {
      out.push_back({s.ics[ics - 1].label, s.ics[ics - 1].color});
    } else if (ics > 0 && ics <= (int)s.ics.size()) {
      out.push_back({c["name"] | "", s.ics[ics - 1].color});
    } else {
      out.push_back({c["name"] | "", parseColor(c["color"])});
    }
  }
  return out;
}

void parseEvents(JsonArray arr, const std::vector<CalRef> &cals, uint8_t account, std::vector<Event> &out) {
  for (JsonObject e : arr) {
    Event ev;
    ev.account = account;
    if (e["k"].is<const char *>()) {  // format 2
      ev.key = e["k"] | "";
      ev.start = e["s"].as<int64_t>();
      ev.end = e["e"].as<int64_t>();
      ev.allDay = e["a"] | false;
      ev.title = e["t"] | "";
      const int ci = e["c"] | -1;
      if (ci >= 0 && ci < (int)cals.size()) {
        ev.calendar = cals[ci].name;
        ev.color = cals[ci].color;
      } else {
        ev.color = parseColor(nullptr);
      }
      ev.location = e["l"] | "";
      ev.description = e["d"] | "";
      ev.organizer = e["o"] | "";
      ev.guests = e["g"] | 0;
      ev.travelMin = e["tr"] | 0;
      out.push_back(ev);
      continue;
    }
    ev.key = e["key"].as<String>();
    ev.start = e["start"].as<int64_t>();
    ev.end = e["end"].as<int64_t>();
    ev.allDay = e["allDay"].as<bool>();
    ev.title = e["title"].as<String>();
    ev.calendar = e["cal"].as<String>();
    ev.color = parseColor(e["color"]);
    ev.location = e["loc"].as<String>();
    ev.description = e["desc"].as<String>();
    ev.organizer = e["org"].as<String>();
    ev.guests = e["guests"].as<uint16_t>();
    ev.travelMin = e["travelMin"] | 0;
    out.push_back(ev);
  }
}

void parseTasks(JsonArray arr, uint8_t account, std::vector<Task> &out) {
  for (JsonObject t : arr) {
    Task task;
    task.account = account;
    task.id = t["id"].as<String>();
    task.listId = t["listId"].as<String>();
    task.title = t["title"].as<String>();
    task.list = t["list"].as<String>();
    task.dueKey = t["dueKey"].as<String>();
    task.notes = t["notes"].as<String>();
    task.parent = t["parent"].as<String>();
    task.completedAt = t["at"].as<long>();
    out.push_back(task);
  }
}

void parseLists(JsonArray arr, uint8_t account, std::vector<TaskList> &out) {
  for (JsonObject l : arr) out.push_back({account, l["id"].as<String>(), l["title"].as<String>()});
}

void parseDates(JsonArray arr, uint8_t account, std::vector<DateItem> &out) {
  for (JsonObject d : arr) {
    const String kind = d["kind"].as<String>();
    DateItem item{kind == "star" ? DateKind::Star : kind == "birthday" ? DateKind::Birthday : DateKind::Holiday,
                  d["title"].as<String>(), d["day"].as<String>(), d["key"].as<String>(), account};
    // Every account sees the same public holidays; keep one of each.
    const bool dup = std::any_of(out.begin(), out.end(), [&](const DateItem &o) {
      return o.kind == item.kind && o.day == item.day && o.title == item.title;
    });
    if (!dup && item.day.length() == 10) out.push_back(item);
  }
}

void parseFrequent(JsonArray arr, std::vector<String> &out) {
  for (JsonVariant v : arr) {
    const String t = v.as<String>();
    if (t.length() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
  }
}

// Everything in a feed besides events and tasks.
struct Extras {
  std::vector<Task> done;
  std::vector<TaskList> lists;
  std::vector<DateItem> dates;
  std::vector<String> frequent;
  std::vector<IcsStatus> ics;  // from the first account's feed
  bool hasIcs = false;
};

void sortDates(std::vector<DateItem> &dates) {
  std::stable_sort(dates.begin(), dates.end(), [](const DateItem &a, const DateItem &b) { return a.day < b.day; });
}

void sortDone(std::vector<Task> &done) {
  std::stable_sort(done.begin(), done.end(), [](const Task &a, const Task &b) { return a.completedAt > b.completedAt; });
}

// Google calendars go to `out` (the "choose calendars" list); published (ICS)
// ones only report how their fetch went.
void parseCalendars(JsonArray arr, std::vector<CalendarInfo> &out, std::vector<IcsStatus> *ics) {
  out.clear();
  for (JsonObject c : arr) {
    const int idx = c["ics"] | 0;
    if (idx > 0) {
      if (ics && idx <= (int)ics->size()) {
        IcsStatus &st = (*ics)[idx - 1];
        st.synced = true;
        st.events = c["n"] | 0;
        st.name = c["name"] | "";
        st.error = c["err"] | "";
      }
      continue;
    }
    out.push_back({c["id"].as<String>(), c["name"].as<String>(), parseColor(c["color"]), c["shown"].as<bool>()});
  }
}

// Fixed-offset POSIX TZ, refreshed on every sync so DST changes are picked up
// within one refresh interval. POSIX offsets are positive west of UTC.
void applyUtcOffset(int minutes) {
  const int west = -minutes;
  char buf[24];
  if (west % 60) {
    snprintf(buf, sizeof(buf), "UTC%+d:%02d", west / 60, abs(west % 60));
  } else {
    snprintf(buf, sizeof(buf), "UTC%+d", west / 60);
  }
  if (timezone == buf) return;
  timezone = buf;
  setenv("TZ", buf, 1);
  tzset();
}

void sortAndFilter(std::vector<Event> &events, std::vector<Task> &tasks) {
  events.erase(std::remove_if(events.begin(), events.end(), [](const Event &ev) {
    return hidden.count(eventHiddenKey(ev.account, ev.key)) > 0;
  }), events.end());
  tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [](const Task &t) {
    return hidden.count(taskHiddenKey(t.account, t.id)) > 0;
  }), tasks.end());
  std::stable_sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
    return a.start < b.start;
  });
  std::stable_sort(tasks.begin(), tasks.end(), [](const Task &a, const Task &b) {
    if (a.dueKey.length() && b.dueKey.length()) return a.dueKey < b.dueKey;
    return a.dueKey.length() > 0 && b.dueKey.length() == 0;
  });
}

time_t addDays(time_t day, int n) {
  struct tm tm;
  localtime_r(&day, &tm);
  tm.tm_mday += n;
  tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
  tm.tm_isdst = -1;
  return mktime(&tm);
}

// Monday 00:00 of this week, local time; 0 while the clock is not set.
time_t thisMonday() {
  const time_t now = time(nullptr);
  if (now < kClockValid) return 0;
  struct tm tm;
  localtime_r(&now, &tm);
  return addDays(now, -((tm.tm_wday + 6) % 7));
}

// Published calendars ride along with the first account's requests.
String icsQuery(const Settings &s) {
  String q;
  for (const IcsCalendar &c : s.ics) {
    if (c.url.length()) q += "&ics=" + urlEncode(c.url);
  }
  return q;
}

// The feed starts on this week's Monday and covers this week and the next, so
// the week view needs no request of its own for them. `since` is the last
// feed's fingerprint: an unchanged feed then answers {"same":true}.
String feedQuery(const Settings &s, uint8_t account, int64_t from, const String &since) {
  String q = kApiVersion;
  if (from > 0) q += "&from=" + String((long long)from);
  if (since.length()) q += "&since=" + urlEncode(since);
  if (account == 0) q += icsQuery(s);
  return q;
}

// True if every account's last feed holds all events of this week.
bool weekCovered(const std::vector<AccountStatus> &accounts, time_t week) {
  if (!week || accounts.empty()) return false;
  const int64_t a = int64_t(week) * 1000, b = int64_t(addDays(week, 7)) * 1000;
  for (const AccountStatus &st : accounts) {
    if (!st.from || st.from > a || st.to < b) return false;
  }
  return true;
}

// Fills the week view from the feed. Call with the lock held.
void weekFromFeed(time_t week) {
  const int64_t a = int64_t(week) * 1000, b = int64_t(addDays(week, 7)) * 1000;
  snapshot.week.clear();
  for (const Event &ev : snapshot.events) {
    if ((ev.end > a || ev.start >= a) && ev.start < b) snapshot.week.push_back(ev);
  }
  snapshot.weekStart = week;
  snapshot.weekLoading = false;
}

// Applies one account's feed reply to the snapshot fields passed in. `same`
// is set when the script says nothing changed since the fingerprint we sent.
bool applyFeed(const String &body, const Settings &s, uint8_t account, std::vector<Event> &events,
               std::vector<Task> &tasks, Extras &extras, AccountStatus &status, String &err, bool &same) {
  same = false;
  JsonDocument doc;
  if (!parseResponse(body, doc, err)) return false;
  if (doc["same"] | false) {
    same = true;
    return true;
  }
  JsonArray calendars = doc["calendars"].as<JsonArray>();
  parseEvents(doc["events"].as<JsonArray>(), calRefs(calendars, s), account, events);
  parseTasks(doc["tasks"].as<JsonArray>(), account, tasks);
  parseTasks(doc["done"].as<JsonArray>(), account, extras.done);
  parseLists(doc["lists"].as<JsonArray>(), account, extras.lists);
  parseDates(doc["dates"].as<JsonArray>(), account, extras.dates);
  parseFrequent(doc["frequent"].as<JsonArray>(), extras.frequent);
  if (account == 0) {
    extras.ics.assign(s.ics.size(), IcsStatus());
    extras.hasIcs = true;
  }
  parseCalendars(calendars, status.calendars, account == 0 ? &extras.ics : nullptr);
  status.email = doc["account"].as<String>();
  status.hash = doc["hash"] | "";
  status.from = doc["from"].as<int64_t>();
  status.to = doc["to"].as<int64_t>();
  if (doc["utcOffsetMin"].is<int>()) applyUtcOffset(doc["utcOffsetMin"].as<int>());
  return true;
}

// Shows whatever the SD card has until the first sync is done.
void loadCache(const Settings &s) {
  if (!s.offlineCopy) return;
  std::vector<Event> events;
  std::vector<Task> tasks;
  Extras extras;
  std::vector<AccountStatus> accounts(s.accounts.size());
  bool any = false;
  for (uint8_t i = 0; i < s.accounts.size(); i++) {
    String body, err;
    bool same;
    if (storage_read(cachePath(s.accounts[i]).c_str(), body) &&
        applyFeed(body, s, i, events, tasks, extras, accounts[i], err, same)) {
      any = true;
    }
  }
  if (!any) return;
  Guard g;
  sortAndFilter(events, tasks);
  sortDates(extras.dates);
  snapshot.events = std::move(events);
  snapshot.tasks = std::move(tasks);
  sortDone(extras.done);
  snapshot.done = std::move(extras.done);
  snapshot.lists = std::move(extras.lists);
  snapshot.dates = std::move(extras.dates);
  snapshot.frequent = std::move(extras.frequent);
  snapshot.accounts = std::move(accounts);
  if (extras.hasIcs) snapshot.ics = std::move(extras.ics);
  snapshot.offline = true;
  snapshot.version++;
}

// One request per account. With `full` false each account sends its last
// fingerprint, and an unchanged account answers in a few bytes: nothing is
// downloaded, parsed or written to the card for it.
void fetchAll(const Settings &s, bool full) {
  const size_t n = s.accounts.size();
  std::vector<Event> events;
  std::vector<Task> tasks;
  Extras extras;
  std::vector<AccountStatus> accounts;
  std::vector<bool> fresh(n, false);  // new data arrived for this account
  {
    Guard g;
    accounts = snapshot.accounts;
  }
  accounts.resize(n);
  const int64_t from = int64_t(thisMonday()) * 1000;

  String errors;
  bool anyOk = false;
  for (uint8_t i = 0; i < n; i++) {
    String body, err;
    AccountStatus status = accounts[i];
    bool same = false;
    const String since = full ? String() : status.hash;
    if (httpGet(accountUrl(s.accounts[i], feedQuery(s, i, from, since)), body, err) &&
        applyFeed(body, s, i, events, tasks, extras, status, err, same)) {
      anyOk = true;
      fresh[i] = !same;
      status.syncedAt = time(nullptr);
      status.error = "";
      if (!same && s.offlineCopy) storage_write(cachePath(s.accounts[i]).c_str(), body);
    } else {
      status.error = err;
      if (errors.length()) errors += kErrJoin;
      errors += strf(kErrAccount, s.accounts[i].label.c_str(), err.c_str());
      log_w("account %s failed: %s", s.accounts[i].label.c_str(), err.c_str());
    }
    accounts[i] = status;
  }

  Guard g;
  // Keep the last good data for any account that failed or did not change.
  for (const Event &ev : snapshot.events) {
    if (ev.account < n && !fresh[ev.account]) events.push_back(ev);
  }
  for (const Task &t : snapshot.tasks) {
    if (t.account < n && !fresh[t.account]) tasks.push_back(t);
  }
  for (const Task &t : snapshot.done) {
    if (t.account < n && !fresh[t.account]) extras.done.push_back(t);
  }
  for (const TaskList &l : snapshot.lists) {
    if (l.account < n && !fresh[l.account]) extras.lists.push_back(l);
  }
  for (const DateItem &d : snapshot.dates) {
    if (d.account < n && !fresh[d.account]) extras.dates.push_back(d);
  }
  if (extras.frequent.empty()) extras.frequent = snapshot.frequent;
  // Actions that have been sent are reflected in this fetch; forget them.
  for (auto it = hidden.begin(); it != hidden.end();) {
    it = it->second ? hidden.erase(it) : std::next(it);
  }
  sortAndFilter(events, tasks);
  sortDates(extras.dates);

  snapshot.events = std::move(events);
  snapshot.tasks = std::move(tasks);
  sortDone(extras.done);
  snapshot.done = std::move(extras.done);
  snapshot.lists = std::move(extras.lists);
  snapshot.dates = std::move(extras.dates);
  snapshot.frequent = std::move(extras.frequent);
  snapshot.accounts = std::move(accounts);
  if (extras.hasIcs) snapshot.ics = std::move(extras.ics);
  snapshot.ics.resize(s.ics.size());
  snapshot.error = errors;
  if (!errors.length()) {
    snapshot.syncedAt = time(nullptr);
    snapshot.offline = false;
  } else if (!anyOk && (snapshot.events.size() || snapshot.tasks.size())) {
    snapshot.offline = true;  // every account failed: what is shown is an older copy
  }
  const time_t week = weekRequested;
  if (weekCovered(snapshot.accounts, week)) weekFromFeed(week);
  snapshot.version++;
}

// The feed about to run will hold this week (this week or the next one).
bool feedWillCover(time_t week) {
  const time_t monday = thisMonday();
  return monday && (week == monday || week == addDays(monday, 7));
}

// A week the feed does not cover: one range request per account.
void fetchWeek(const Settings &s, time_t weekStart) {
  const int64_t from = int64_t(weekStart) * 1000;
  const int64_t to = int64_t(addDays(weekStart, 7)) * 1000;
  String query = String(kApiVersion) + "&action=range&from=" + String((long long)from) + "&to=" + String((long long)to);
  std::vector<Event> events;
  String errors;
  for (uint8_t i = 0; i < s.accounts.size(); i++) {
    String body, err;
    JsonDocument doc;
    String q = query;
    if (i == 0) q += icsQuery(s);
    if (httpGet(accountUrl(s.accounts[i], q), body, err) && parseResponse(body, doc, err)) {
      parseEvents(doc["events"].as<JsonArray>(), calRefs(doc["calendars"].as<JsonArray>(), s), i, events);
    } else {
      if (errors.length()) errors += kErrJoin;
      errors += strf(kErrAccount, s.accounts[i].label.c_str(), err.c_str());
    }
  }
  std::vector<Task> none;
  Guard g;
  sortAndFilter(events, none);
  if (weekRequested != weekStart) return;  // the user already moved on
  snapshot.week = std::move(events);
  snapshot.weekStart = weekStart;
  snapshot.weekLoading = false;
  if (errors.length()) snapshot.error = errors;
  snapshot.version++;
}

const char *actionName(ActionType t) {
  switch (t) {
    case ActionType::Dismiss: return kActDismiss;
    case ActionType::Complete: return kActComplete;
    case ActionType::Uncomplete: return kActUndo;
    case ActionType::ShowCalendar: return kActCalendar;
    case ActionType::AddTask: return kActAdd;
    case ActionType::SetDue: return kActDue;
    case ActionType::Star: return kActStar;
  }
  return kActOther;
}

bool isLocal(const String &taskId) { return taskId.startsWith(kLocalPrefix); }

// Puts Google's ids into an action on a task added on the display. Returns
// false if that task was never created (its add failed): nothing to send.
// Call with the lock held.
bool resolveTask(Action &a) {
  if (!isLocal(a.taskId)) return true;
  auto it = addedIds.find(a.taskId);
  if (it == addedIds.end()) {
    if (a.hiddenKey.length()) hidden.erase(a.hiddenKey);
    return false;
  }
  a.taskId = it->second.first;
  a.listId = it->second.second;
  if (a.hiddenKey.length()) {
    // The next sync lists the task under its real id: hide that one.
    hidden.erase(a.hiddenKey);
    a.hiddenKey = taskHiddenKey(a.account, a.taskId);
    hidden[a.hiddenKey] = false;
    const String id = a.taskId;
    const uint8_t account = a.account;
    auto &list = snapshot.tasks;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Task &t) {
      return t.account == account && t.id == id;
    }), list.end());
    snapshot.version++;
  }
  return true;
}

// A task added here now has Google's ids: use them on screen and for later actions.
void taskAdded(const Action &a, JsonDocument &doc) {
  const char *id = doc["id"].as<const char *>();
  if (!id || !*id) return;
  const char *list = doc["listId"].as<const char *>();
  const String listId = list && *list ? String(list) : a.listId;
  addedIds[a.taskId] = {String(id), listId};
  for (Task &t : snapshot.tasks) {
    if (t.account == a.account && t.id == a.taskId) {
      t.id = id;
      t.listId = listId;
    }
  }
  snapshot.version++;
}

// Returns true if any action was sent, so the caller re-syncs.
bool runPendingActions(const Settings &s) {
  bool ran = false;
  for (;;) {
    Action a;
    {
      Guard g;
      if (pending.empty()) break;
      a = pending.front();
      pending.pop_front();
      if (a.account >= s.accounts.size()) continue;
      if (a.type != ActionType::AddTask && a.taskId.length() && !resolveTask(a)) continue;
    }
    String query = a.query;
    if (a.type != ActionType::AddTask && a.taskId.length()) {
      query += "&list=" + urlEncode(a.listId) + "&task=" + urlEncode(a.taskId);
    }
    String body, err;
    JsonDocument doc;
    int code = 0;
    const bool ok = httpGet(accountUrl(s.accounts[a.account], query), body, err, &code) &&
                    parseResponse(body, doc, err);
    ran = true;

    Guard g;
    // No connection (Wi-Fi hiccup, TLS short on memory): try again shortly.
    // Not for adds: the script may have run before the reply was lost, and a
    // second try would add the task twice. Every other action is idempotent.
    if (!ok && code < 0 && a.type != ActionType::AddTask && ++a.tries < kActionTries) {
      pending.push_front(a);
      log_w("action failed, will retry: %s", err.c_str());
      break;
    }
    if (ok && a.type == ActionType::AddTask) taskAdded(a, doc);
    if (a.hiddenKey.length()) {
      if (ok) {
        hidden[a.hiddenKey] = true;
      } else {
        hidden.erase(a.hiddenKey);  // let the item come back on the next sync
      }
    }
    if (!ok) {
      snapshot.error = strf(kErrAction, actionName(a.type), err.c_str());
      snapshot.version++;
      log_w("action failed: %s", err.c_str());
    }
  }
  return ran;
}

void setWifiError(bool up, WifiState st) {
  Guard g;
  const char *msg = nullptr;
  if (st == WifiState::NotConfigured) msg = kErrNoWifiSetup;
  else if (!up) msg = kErrNoWifi;
  if (msg && snapshot.error != msg) {
    snapshot.error = msg;
    snapshot.version++;
  }
}

void workerLoop(void *) {
  Settings s = settings_get();
  uint32_t settingsVersion = settings_version();
  loadCache(s);

  uint32_t lastFetch = 0;
  bool clockSet = false;
  for (;;) {
    if (settings_version() != settingsVersion) {
      const Settings next = settings_get();
      settingsVersion = settings_version();
      bool accountsChanged = next.accounts.size() != s.accounts.size();
      for (size_t i = 0; !accountsChanged && i < next.accounts.size(); i++) {
        accountsChanged = next.accounts[i].url != s.accounts[i].url || next.accounts[i].token != s.accounts[i].token;
      }
      bool icsChanged = next.ics.size() != s.ics.size();
      for (size_t i = 0; !icsChanged && i < next.ics.size(); i++) {
        icsChanged = next.ics[i].url != s.ics[i].url || next.ics[i].label != s.ics[i].label ||
                     next.ics[i].color != s.ics[i].color;
      }
      s = next;
      if (accountsChanged) {
        // Indexes moved; start clean rather than mislabel items.
        Guard g;
        snapshot.events.clear();
        snapshot.tasks.clear();
        snapshot.week.clear();
        snapshot.accounts.assign(s.accounts.size(), AccountStatus());
        snapshot.ics.assign(s.ics.size(), IcsStatus());
        hidden.clear();
        pending.clear();
        addedIds.clear();
        snapshot.version++;
        refreshRequested = true;
      } else if (icsChanged) {
        Guard g;
        snapshot.ics.assign(s.ics.size(), IcsStatus());
        snapshot.version++;
        refreshRequested = true;
        fullRequested = true;
      }
    }

    const bool up = wifi_service();
    setWifiError(up, wifi_state());
    if (up) {
      if (!clockSet) {
        configTime(0, 0, kNtpServer1, kNtpServer2);
        if (timezone.length()) {
          setenv("TZ", timezone.c_str(), 1);
          tzset();
        }
        clockSet = true;
        // The first feed asks for this week from Monday, so give NTP a moment.
        for (int i = 0; i < 40 && time(nullptr) < kClockValid; i++) delay(100);
      }
      const bool acted = runPendingActions(s);
      const bool due = lastFetch == 0 || millis() - lastFetch >= s.refreshMinutes * 60000UL;
      const bool fetch = s.accounts.size() && (acted || due || refreshRequested);
      const time_t week = weekRequested;
      bool needWeek;
      {
        Guard g;
        needWeek = week && snapshot.weekLoading;
      }
      // A week the user is waiting for goes first, unless the feed about to
      // run holds it anyway.
      if (needWeek && !(fetch && feedWillCover(week))) {
        fetchWeek(s, week);
        needWeek = false;
      }
      if (fetch) {
        // After a tap (or "Sync now") take the whole feed, so nothing local
        // is kept that the server no longer has.
        const bool full = acted || fullRequested;
        refreshRequested = false;
        fullRequested = false;
        fetchAll(s, full);
        lastFetch = millis();
      }
      if (needWeek) {
        bool still;
        {
          Guard g;
          still = weekRequested == week && snapshot.weekLoading;
        }
        if (still) fetchWeek(s, week);
      }
      closeIdleConnections();
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(up ? kIdleLoopMs : kOfflineLoopMs));
  }
}

void wake() {
  if (worker) xTaskNotifyGive(worker);
}

}  // namespace

void sync_start() {
  lock = xSemaphoreCreateMutex();
  const Settings s = settings_get();
  snapshot.accounts.resize(s.accounts.size());
  snapshot.ics.resize(s.ics.size());
  // Core 0 alongside the Wi-Fi stack; LVGL runs in loop() on core 1.
  xTaskCreatePinnedToCore(workerLoop, "sync", kTaskStack, nullptr, 1, &worker, 0);
}

bool sync_get_if_newer(uint32_t version, Snapshot &out) {
  Guard g;
  if (snapshot.version == version) return false;
  out = snapshot;
  return true;
}

void sync_refresh() {
  refreshRequested = true;
  fullRequested = true;
  wake();
}

void sync_request_week(time_t weekStart) {
  bool loading;
  {
    Guard g;
    weekRequested = weekStart;
    // This week and the next are in the feed (or the SD copy of it): no wait.
    if (weekCovered(snapshot.accounts, weekStart)) {
      weekFromFeed(weekStart);
    } else {
      snapshot.weekLoading = true;
    }
    loading = snapshot.weekLoading;
    snapshot.version++;
  }
  if (loading) wake();
}

void sync_dismiss_event(const Event &ev) {
  const String hk = eventHiddenKey(ev.account, ev.key);
  {
    Guard g;
    auto drop = [&](std::vector<Event> &list) {
      list.erase(std::remove_if(list.begin(), list.end(), [&](const Event &e) {
        return e.account == ev.account && e.key == ev.key;
      }), list.end());
    };
    drop(snapshot.events);
    drop(snapshot.week);
    hidden[hk] = false;
    pending.push_back({ActionType::Dismiss, ev.account, hk,
                       "action=dismiss&event=" + urlEncode(ev.key) + "&until=" + String((long long)ev.end)});
    snapshot.version++;
  }
  wake();
}

void sync_complete_task(const Task &task) {
  const String hk = taskHiddenKey(task.account, task.id);
  {
    Guard g;
    auto &list = snapshot.tasks;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Task &t) {
      return t.account == task.account && t.id == task.id;
    }), list.end());
    hidden[hk] = false;
    Task done = task;
    done.completedAt = time(nullptr);
    snapshot.done.insert(snapshot.done.begin(), done);
    pending.push_back({ActionType::Complete, task.account, hk, "action=complete", task.id, task.listId});
    snapshot.version++;
  }
  wake();
}

void sync_uncomplete_task(const Task &task) {
  const String hk = taskHiddenKey(task.account, task.id);
  {
    Guard g;
    // If the completion has not been sent yet, just cancel it.
    auto queued = std::find_if(pending.begin(), pending.end(), [&](const Action &a) {
      return a.type == ActionType::Complete && a.hiddenKey == hk;
    });
    if (queued != pending.end()) {
      pending.erase(queued);
    } else {
      pending.push_back({ActionType::Uncomplete, task.account, "", "action=uncomplete", task.id, task.listId});
    }
    hidden.erase(hk);
    snapshot.done.erase(std::remove_if(snapshot.done.begin(), snapshot.done.end(), [&](const Task &t) {
      return t.account == task.account && t.id == task.id;
    }), snapshot.done.end());
    snapshot.tasks.push_back(task);
    snapshot.tasks.back().completedAt = 0;
    std::vector<Event> none;
    sortAndFilter(none, snapshot.tasks);
    snapshot.version++;
  }
  wake();
}

void sync_show_calendar(uint8_t account, const String &calendarId, bool shown) {
  {
    Guard g;
    if (account < snapshot.accounts.size()) {
      for (CalendarInfo &c : snapshot.accounts[account].calendars) {
        if (c.id == calendarId) c.shown = shown;
      }
    }
    pending.push_back({ActionType::ShowCalendar, account, "",
                       "action=showcal&id=" + urlEncode(calendarId) + "&shown=" + (shown ? "1" : "0")});
    snapshot.version++;
  }
  wake();
}

void sync_add_task(uint8_t account, const String &listId, const String &title, const String &dueKey) {
  {
    Guard g;
    // Shown straight away; the next sync replaces it with the real task.
    Task t;
    t.account = account;
    t.id = kLocalPrefix + String(millis());
    t.listId = listId;
    t.title = title;
    t.dueKey = dueKey;
    for (const TaskList &l : snapshot.lists) {
      if (l.account == account && l.id == listId) t.list = l.title;
    }
    snapshot.tasks.push_back(t);
    std::vector<Event> none;
    sortAndFilter(none, snapshot.tasks);
    String q = "action=add&title=" + urlEncode(title);
    if (listId.length()) q += "&list=" + urlEncode(listId);
    if (dueKey.length()) q += "&due=" + dueKey;
    pending.push_back({ActionType::AddTask, account, "", q, t.id, listId});
    snapshot.version++;
  }
  wake();
}

void sync_set_task_due(const Task &task, const String &dueKey) {
  {
    Guard g;
    for (Task &t : snapshot.tasks) {
      if (t.account == task.account && t.id == task.id) t.dueKey = dueKey;
    }
    std::vector<Event> none;
    sortAndFilter(none, snapshot.tasks);
    pending.push_back({ActionType::SetDue, task.account, "", "action=due&due=" + dueKey, task.id, task.listId});
    snapshot.version++;
  }
  wake();
}

void sync_star_event(const Event &ev, bool on, const String &dayKey) {
  {
    Guard g;
    auto &d = snapshot.dates;
    d.erase(std::remove_if(d.begin(), d.end(), [&](const DateItem &x) {
      return x.kind == DateKind::Star && x.key == ev.key;
    }), d.end());
    if (on) {
      d.push_back({DateKind::Star, ev.title, dayKey, ev.key, ev.account});
      sortDates(d);
    }
    pending.push_back({ActionType::Star, ev.account, "",
                       "action=star&event=" + urlEncode(ev.key) + "&on=" + (on ? "1" : "0") + "&title=" +
                           urlEncode(ev.title) + "&start=" + String((long long)ev.start) + "&allDay=" +
                           (ev.allDay ? "1" : "0")});
    snapshot.version++;
  }
  wake();
}
