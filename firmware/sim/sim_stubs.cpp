// Stand-ins for the device modules so the real UI code runs on a PC with
// sample data. Touch actions update the sample data the same way sync does.
#include <algorithm>
#include <vector>

#include "audio.h"
#include "battery.h"
#include "ota.h"
#include "desk.h"
#include "presence.h"
#include "sensors.h"
#include "settings.h"
#include "storage.h"
#include "sync.h"
#include "ui.h"
#include "web.h"
#include "wifi_mgr.h"

extern Snapshot g_sim;
extern time_t g_sim_now;
static Settings simSettings;
static uint32_t simSettingsVersion = 1;

uint32_t g_sim_ms = 0;  // the simulator jumps it forward to run throttled ticks
extern "C" uint32_t millis(void) { return g_sim_ms += 5; }

void settings_begin() {}
Settings settings_get() { return simSettings; }
void settings_save(const Settings &s) {
  simSettings = s;
  simSettingsVersion++;
}
uint32_t settings_version() { return simSettingsVersion; }

void sync_start() {}
bool sync_get_if_newer(uint32_t version, Snapshot &out) {
  if (g_sim.version == version) return false;
  out = g_sim;
  return true;
}
void sync_refresh() {}
void sync_request_week(time_t weekStart) {
  g_sim.weekStart = weekStart;
  g_sim.weekLoading = false;
  g_sim.version++;
}
void sync_dismiss_event(const Event &ev) {
  auto &l = g_sim.events;
  l.erase(std::remove_if(l.begin(), l.end(), [&](const Event &e) { return e.key == ev.key; }), l.end());
  g_sim.version++;
}
void sync_complete_task(const Task &task) {
  auto &l = g_sim.tasks;
  l.erase(std::remove_if(l.begin(), l.end(), [&](const Task &t) { return t.id == task.id; }), l.end());
  g_sim.done.insert(g_sim.done.begin(), task);
  g_sim.version++;
}
void sync_uncomplete_task(const Task &task) {
  auto &d = g_sim.done;
  d.erase(std::remove_if(d.begin(), d.end(), [&](const Task &t) { return t.id == task.id; }), d.end());
  g_sim.tasks.push_back(task);
  g_sim.version++;
}
void sync_show_calendar(uint8_t, const String &, bool) {}
void sync_add_task(uint8_t account, const String &listId, const String &title, const String &dueKey) {
  Task t;
  t.account = account;
  t.id = "new:" + title;
  t.listId = listId;
  t.title = title;
  t.list = "My Tasks";
  t.dueKey = dueKey;
  g_sim.tasks.push_back(t);
  g_sim.version++;
}
void sync_set_task_due(const Task &task, const String &dueKey) {
  for (Task &t : g_sim.tasks) {
    if (t.id == task.id) t.dueKey = dueKey;
  }
  g_sim.version++;
}
void sync_star_event(const Event &ev, bool on, const String &dayKey) {
  auto &l = g_sim.dates;
  l.erase(std::remove_if(l.begin(), l.end(), [&](const DateItem &d) { return d.key == ev.key; }), l.end());
  if (on) l.push_back({DateKind::Star, ev.title, dayKey, ev.key, ev.account});
  std::sort(l.begin(), l.end(), [](const DateItem &a, const DateItem &b) { return strcmp(a.day.c_str(), b.day.c_str()) < 0; });
  g_sim.version++;
}

void wifi_begin() {}
bool wifi_service() { return true; }
WifiState wifi_state() { return WifiState::Connected; }
String wifi_ssid() { return "Popescu-5G"; }
int wifi_rssi() { return -52; }
String wifi_ip() { return "192.168.1.42"; }
void wifi_scan_start() {}
bool wifi_scan_results(std::vector<WifiNetwork> &out) {
  out = {{"Popescu-5G", -48, true}, {"Popescu-2.4", -55, true}, {"DIGI-7Q2X", -67, true},
         {"Neighbour Guest", -70, false}, {"Printer-HP-4F21", -80, false}, {"TP-Link_3A9C", -84, true}};
  return true;
}
void wifi_connect(const String &, const String &) {}

void ota_begin() {}
OtaStatus g_sim_ota;
OtaStatus ota_status() { return g_sim_ota; }
void ota_check() {}
void ota_install() {}
bool ota_upload_start(size_t) { return true; }
bool ota_upload_write(const uint8_t *, size_t) { return true; }
bool ota_upload_finish() { return true; }
void ota_upload_abort(const String &) {}

void web_begin() {}
String web_pin() { return "482913"; }
String web_url() { return "http://calendar.local"; }

void audio_begin() {}
void audio_chime(uint8_t) {}
void audio_tick(uint8_t) {}
void audio_alert(uint8_t) {}
void audio_done(uint8_t) {}

void sensors_begin() {}
bool light_present() { return true; }
int light_lux() { return 180; }
bool motion_seen() { return false; }

// Phone presence: sim_main sets g_sim_phone and g_sim_seen_ago to try the layouts.
PhoneState g_sim_phone = PhoneState::Near;
long g_sim_seen_ago = 20;
void presence_apply(bool) {}
const char *presence_error() { return nullptr; }
bool presence_ready() { return true; }
PhoneState presence_state(uint8_t) { return g_sim_phone; }
long presence_seen_ago() { return g_sim_seen_ago; }
void presence_pair(bool) {}
bool presence_pairing() { return true; }
int presence_paired_count() { return 1; }
void presence_forget() {}

// Smart desk: sitting at M1 with the timer running; sim_main can change it.
DeskStatus g_sim_desk = [] {
  DeskStatus d;
  d.online = d.loggedIn = d.hasLogin = true;
  d.heightMm = 802;
  d.state = "SITTING";
  d.memoryMm[0] = 720;
  d.memoryMm[1] = 1120;
  d.memoryMm[2] = 950;
  d.memoryMm[3] = 1180;
  d.timerRunning = true;
  d.timerPhase = "SIT";
  d.timerLeft = 12 * 60 + 20;
  d.sitMinutes = d.standMinutes = 30;
  return d;
}();
void desk_begin() {}
DeskStatus desk_status() { return g_sim_desk; }
void desk_recall(uint8_t slot) { g_sim_desk.heightMm = g_sim_desk.memoryMm[slot - 1]; }
void desk_hold(int) {}
void desk_stop() {}
void desk_timer(bool on) { g_sim_desk.timerRunning = on; }
void desk_away(bool) {}
void desk_timer_apply() {}
void desk_test(const String &, const String &, const String &) {}
DeskTestResult desk_test_result() {
  DeskTestResult r;
  r.done = true;
  r.ok = true;
  r.message = "Desk found (80.2 cm). Login accepted.";
  return r;
}


void battery_begin() {}
bool battery_present() { return true; }
int battery_percent() { return 78; }
bool battery_charging() { return true; }

bool storage_begin() { return true; }
bool storage_ok() { return true; }
uint64_t storage_free_bytes() { return 7200000000ULL; }
bool storage_write(const char *, const String &) { return true; }
bool storage_read(const char *, String &) { return false; }

void ui_set_backlight(uint8_t) {}
time_t ui_now() { return g_sim_now; }

void sim_set_settings(const Settings &s) {
  simSettings = s;
  simSettingsVersion++;
}
