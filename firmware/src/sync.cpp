#include <Arduino.h>
#include <stdarg.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <deque>
#include <map>

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
constexpr uint32_t kIdleLoopMs = 1000;       // how often the worker wakes when online
constexpr uint32_t kOfflineLoopMs = 5000;    // ...and when offline
constexpr uint32_t kTaskStack = 16384;

// ---- State -------------------------------------------------------------------

enum class ActionType { Dismiss, Complete, Uncomplete, ShowCalendar, AddTask, SetDue, Star };

struct Action {
  ActionType type;
  uint8_t account;
  String hiddenKey;  // entry in `hidden` this action covers, if any
  String query;      // extra query string for the Apps Script call
};

SemaphoreHandle_t lock;
TaskHandle_t worker;
Snapshot snapshot;
std::deque<Action> pending;
// Items removed on screen but not yet confirmed by a fresh sync. Value is
// true once the action has been sent, so the next sync can drop the entry.
std::map<String, bool> hidden;
volatile bool refreshRequested = true;
volatile time_t weekRequested = 0;
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

// Apps Script answers on script.google.com with a redirect to
// script.googleusercontent.com, so redirects must be followed.
bool httpGet(const String &url, String &body, String &err) {
  WiFiClientSecure client;
  client.setCACert(GOOGLE_ROOT_CAS);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kReplyTimeoutMs);
  if (!http.begin(client, url)) {
    err = kErrBadUrl;
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    err = code < 0 ? HTTPClient::errorToString(code) : strf(kErrHttp, code);
    // HTTPClient says "connection refused" for any failed connect, a failed
    // secure handshake (e.g. short on memory) included: add the TLS reason.
    char tls[96];
    if (code < 0 && client.lastError(tls, sizeof(tls))) err += String(" (") + tls + ")";
    http.end();
    return false;
  }
  body = http.getString();
  http.end();
  return true;
}

bool parseResponse(const String &body, JsonDocument &doc, String &err) {
  const DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    err = strf(kErrBadReply, jerr.c_str());
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

void parseEvents(JsonArray arr, uint8_t account, std::vector<Event> &out) {
  for (JsonObject e : arr) {
    Event ev;
    ev.account = account;
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
  std::vector<TaskList> lists;
  std::vector<DateItem> dates;
  std::vector<String> frequent;
};

void sortDates(std::vector<DateItem> &dates) {
  std::stable_sort(dates.begin(), dates.end(), [](const DateItem &a, const DateItem &b) { return a.day < b.day; });
}

void parseCalendars(JsonArray arr, std::vector<CalendarInfo> &out) {
  out.clear();
  for (JsonObject c : arr) {
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

// Applies one account's feed reply to the snapshot fields passed in.
bool applyFeed(const String &body, uint8_t account, std::vector<Event> &events,
               std::vector<Task> &tasks, Extras &extras, AccountStatus &status, String &err) {
  JsonDocument doc;
  if (!parseResponse(body, doc, err)) return false;
  parseEvents(doc["events"].as<JsonArray>(), account, events);
  parseTasks(doc["tasks"].as<JsonArray>(), account, tasks);
  parseLists(doc["lists"].as<JsonArray>(), account, extras.lists);
  parseDates(doc["dates"].as<JsonArray>(), account, extras.dates);
  parseFrequent(doc["frequent"].as<JsonArray>(), extras.frequent);
  parseCalendars(doc["calendars"].as<JsonArray>(), status.calendars);
  status.email = doc["account"].as<String>();
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
    if (storage_read(cachePath(s.accounts[i]).c_str(), body) &&
        applyFeed(body, i, events, tasks, extras, accounts[i], err)) {
      any = true;
    }
  }
  if (!any) return;
  Guard g;
  sortAndFilter(events, tasks);
  sortDates(extras.dates);
  snapshot.events = std::move(events);
  snapshot.tasks = std::move(tasks);
  snapshot.lists = std::move(extras.lists);
  snapshot.dates = std::move(extras.dates);
  snapshot.frequent = std::move(extras.frequent);
  snapshot.accounts = std::move(accounts);
  snapshot.offline = true;
  snapshot.version++;
}

void fetchAll(const Settings &s) {
  const size_t n = s.accounts.size();
  std::vector<Event> events;
  std::vector<Task> tasks;
  Extras extras;
  std::vector<AccountStatus> accounts;
  std::vector<bool> fetched(n, false);
  {
    Guard g;
    accounts = snapshot.accounts;
  }
  accounts.resize(n);

  String errors;
  for (uint8_t i = 0; i < n; i++) {
    String body, err;
    AccountStatus status = accounts[i];
    if (httpGet(accountUrl(s.accounts[i], ""), body, err) &&
        applyFeed(body, i, events, tasks, extras, status, err)) {
      fetched[i] = true;
      status.syncedAt = time(nullptr);
      status.error = "";
      if (s.offlineCopy) storage_write(cachePath(s.accounts[i]).c_str(), body);
    } else {
      status.error = err;
      if (errors.length()) errors += kErrJoin;
      errors += strf(kErrAccount, s.accounts[i].label.c_str(), err.c_str());
      log_w("account %s failed: %s", s.accounts[i].label.c_str(), err.c_str());
    }
    accounts[i] = status;
  }

  Guard g;
  // Keep showing the last good data for any account that failed this time.
  for (const Event &ev : snapshot.events) {
    if (ev.account < n && !fetched[ev.account]) events.push_back(ev);
  }
  for (const Task &t : snapshot.tasks) {
    if (t.account < n && !fetched[t.account]) tasks.push_back(t);
  }
  for (const TaskList &l : snapshot.lists) {
    if (l.account < n && !fetched[l.account]) extras.lists.push_back(l);
  }
  for (const DateItem &d : snapshot.dates) {
    if (d.account < n && !fetched[d.account]) extras.dates.push_back(d);
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
  snapshot.lists = std::move(extras.lists);
  snapshot.dates = std::move(extras.dates);
  snapshot.frequent = std::move(extras.frequent);
  snapshot.accounts = std::move(accounts);
  snapshot.error = errors;
  if (!errors.length()) {
    snapshot.syncedAt = time(nullptr);
    snapshot.offline = false;
  }
  snapshot.version++;
}

void fetchWeek(const Settings &s, time_t weekStart) {
  const int64_t from = int64_t(weekStart) * 1000;
  const int64_t to = from + 7LL * 86400 * 1000;
  const String query = "action=range&from=" + String((long long)from) + "&to=" + String((long long)to);
  std::vector<Event> events;
  String errors;
  for (uint8_t i = 0; i < s.accounts.size(); i++) {
    String body, err;
    JsonDocument doc;
    if (httpGet(accountUrl(s.accounts[i], query), body, err) && parseResponse(body, doc, err)) {
      parseEvents(doc["events"].as<JsonArray>(), i, events);
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
    }
    if (a.account >= s.accounts.size()) continue;
    String body, err;
    JsonDocument doc;
    const bool ok = httpGet(accountUrl(s.accounts[a.account], a.query), body, err) &&
                    parseResponse(body, doc, err);
    ran = true;

    Guard g;
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
      s = next;
      if (accountsChanged) {
        // Indexes moved; start clean rather than mislabel items.
        Guard g;
        snapshot.events.clear();
        snapshot.tasks.clear();
        snapshot.week.clear();
        snapshot.accounts.assign(s.accounts.size(), AccountStatus());
        hidden.clear();
        pending.clear();
        snapshot.version++;
        refreshRequested = true;
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
      }
      const bool acted = runPendingActions(s);
      const bool due = lastFetch == 0 || millis() - lastFetch >= s.refreshMinutes * 60000UL;
      if (s.accounts.size() && (acted || due || refreshRequested)) {
        refreshRequested = false;
        fetchAll(s);
        lastFetch = millis();
      }
      const time_t week = weekRequested;
      bool needWeek;
      {
        Guard g;
        needWeek = week && snapshot.weekLoading;
      }
      if (needWeek) fetchWeek(s, week);
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
  snapshot.accounts.resize(settings_get().accounts.size());
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
  wake();
}

void sync_request_week(time_t weekStart) {
  {
    Guard g;
    weekRequested = weekStart;
    snapshot.weekLoading = true;
    snapshot.version++;
  }
  wake();
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
    pending.push_back({ActionType::Complete, task.account, hk,
                       "action=complete&list=" + urlEncode(task.listId) + "&task=" + urlEncode(task.id)});
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
      pending.push_back({ActionType::Uncomplete, task.account, "",
                         "action=uncomplete&list=" + urlEncode(task.listId) + "&task=" + urlEncode(task.id)});
    }
    hidden.erase(hk);
    snapshot.tasks.push_back(task);
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
    t.id = "new:" + String(millis());
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
    pending.push_back({ActionType::AddTask, account, "", q});
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
    pending.push_back({ActionType::SetDue, task.account, "",
                       "action=due&list=" + urlEncode(task.listId) + "&task=" + urlEncode(task.id) + "&due=" + dueKey});
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
