#include <ArduinoJson.h>
#include <Preferences.h>

#include "settings.h"
#include "storage.h"

namespace {

constexpr const char *kBackupPath = "/calendar/settings.json";

SemaphoreHandle_t lock;
Settings current;
uint32_t version = 1;

// Writing to flash pauses the PSRAM cache, and with it the screen's video
// feed, so the picture jumps. Saves wait until the settings have been left
// alone for a few seconds: flipping several toggles costs one write.
constexpr uint32_t kSaveDelayMs = 5000;
bool dirty = false;
uint32_t changedAt = 0;

String toJson(const Settings &s) {
  JsonDocument doc;
  doc["wifiSsid"] = s.wifiSsid;
  doc["wifiPassword"] = s.wifiPassword;
  JsonArray accounts = doc["accounts"].to<JsonArray>();
  for (const Account &a : s.accounts) {
    JsonObject o = accounts.add<JsonObject>();
    o["label"] = a.label;
    o["url"] = a.url;
    o["token"] = a.token;
    o["color"] = a.color;
  }
  doc["brightness"] = s.brightness;
  doc["volume"] = s.volume;
  doc["chime"] = s.chime;
  doc["remindMinutes"] = s.remindMinutes;
  doc["nightMode"] = s.nightMode;
  doc["nightStart"] = s.nightStart;
  doc["nightEnd"] = s.nightEnd;
  doc["dimIdle"] = s.dimIdle;
  doc["dimAfterMinutes"] = s.dimAfterMinutes;
  doc["use24h"] = s.use24h;
  doc["refreshMinutes"] = s.refreshMinutes;
  doc["theme"] = s.theme;
  doc["updateUrl"] = s.updateUrl;
  doc["autoUpdate"] = s.autoUpdate;
  doc["sound"] = s.sound;
  doc["summary"] = s.summary;
  doc["summaryAt"] = s.summaryAt;
  doc["review"] = s.review;
  doc["reviewAt"] = s.reviewAt;
  doc["conflicts"] = s.conflicts;
  doc["leaveBy"] = s.leaveBy;
  doc["leaveLead"] = s.leaveLead;
  doc["focus"] = s.focus;
  doc["focusMinutes"] = s.focusMinutes;
  doc["quickAdd"] = s.quickAdd;
  doc["countdowns"] = s.countdowns;
  doc["dates"] = s.dates;
  doc["ambient"] = s.ambient;
  doc["motion"] = s.motion;
  doc["offlineCopy"] = s.offlineCopy;
  doc["meetTimer"] = s.meetTimer;
  doc["presence"] = s.presence;
  doc["awayMinutes"] = s.awayMinutes;
  doc["costMeter"] = s.costMeter;
  doc["costRate"] = s.costRate;
  doc["radar"] = s.radar;
  doc["radarMoves"] = s.radarMoves;
  doc["roulette"] = s.roulette;
  doc["desk"] = s.desk;
  doc["deskHost"] = s.deskHost;
  doc["deskUser"] = s.deskUser;
  doc["deskPassword"] = s.deskPassword;
  doc["deskSitSlot"] = s.deskSitSlot;
  doc["deskStandSlot"] = s.deskStandSlot;
  doc["deskMeetings"] = s.deskMeetings;
  doc["deskFocus"] = s.deskFocus;
  doc["deskAway"] = s.deskAway;
  JsonArray layouts = doc["layouts"].to<JsonArray>();
  for (uint8_t v : s.layouts) layouts.add(v);
  String out;
  serializeJson(doc, out);
  return out;
}

bool fromJson(const String &json, Settings &s) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  Settings d;  // defaults for anything missing
  s.wifiSsid = doc["wifiSsid"] | "";
  s.wifiPassword = doc["wifiPassword"] | "";
  s.accounts.clear();
  for (JsonObject o : doc["accounts"].as<JsonArray>()) {
    s.accounts.push_back({o["label"] | "", o["url"] | "", o["token"] | "", o["color"] | 0x6C8CFFu});
  }
  s.brightness = doc["brightness"] | d.brightness;
  s.volume = doc["volume"] | d.volume;
  s.chime = doc["chime"] | d.chime;
  s.remindMinutes = doc["remindMinutes"] | d.remindMinutes;
  s.nightMode = doc["nightMode"] | d.nightMode;
  s.nightStart = doc["nightStart"] | d.nightStart;
  s.nightEnd = doc["nightEnd"] | d.nightEnd;
  s.dimIdle = doc["dimIdle"] | d.dimIdle;
  s.dimAfterMinutes = doc["dimAfterMinutes"] | d.dimAfterMinutes;
  s.use24h = doc["use24h"] | d.use24h;
  s.refreshMinutes = max<uint8_t>(1, doc["refreshMinutes"] | d.refreshMinutes);
  s.theme = doc["theme"] | d.theme;
  s.updateUrl = doc["updateUrl"] | d.updateUrl;
  s.autoUpdate = doc["autoUpdate"] | d.autoUpdate;
  s.sound = doc["sound"] | d.sound;
  s.summary = doc["summary"] | d.summary;
  s.summaryAt = doc["summaryAt"] | d.summaryAt;
  s.review = doc["review"] | d.review;
  s.reviewAt = doc["reviewAt"] | d.reviewAt;
  s.conflicts = doc["conflicts"] | d.conflicts;
  s.leaveBy = doc["leaveBy"] | d.leaveBy;
  s.leaveLead = doc["leaveLead"] | d.leaveLead;
  s.focus = doc["focus"] | d.focus;
  s.focusMinutes = max<uint8_t>(1, doc["focusMinutes"] | d.focusMinutes);
  s.quickAdd = doc["quickAdd"] | d.quickAdd;
  s.countdowns = doc["countdowns"] | d.countdowns;
  s.dates = doc["dates"] | d.dates;
  s.ambient = doc["ambient"] | d.ambient;
  s.motion = doc["motion"] | d.motion;
  s.offlineCopy = doc["offlineCopy"] | d.offlineCopy;
  s.meetTimer = doc["meetTimer"] | d.meetTimer;
  s.presence = doc["presence"] | d.presence;
  s.awayMinutes = max<uint8_t>(1, doc["awayMinutes"] | d.awayMinutes);
  s.costMeter = doc["costMeter"] | d.costMeter;
  s.costRate = doc["costRate"] | d.costRate;
  s.radar = doc["radar"] | d.radar;
  s.radarMoves = max<uint8_t>(1, doc["radarMoves"] | d.radarMoves);
  s.roulette = doc["roulette"] | d.roulette;
  s.desk = doc["desk"] | d.desk;
  s.deskHost = doc["deskHost"] | d.deskHost;
  s.deskUser = doc["deskUser"] | d.deskUser;
  s.deskPassword = doc["deskPassword"] | d.deskPassword;
  s.deskSitSlot = constrain(doc["deskSitSlot"] | d.deskSitSlot, 1, 4);
  s.deskStandSlot = constrain(doc["deskStandSlot"] | d.deskStandSlot, 1, 4);
  s.deskMeetings = doc["deskMeetings"] | d.deskMeetings;
  s.deskFocus = doc["deskFocus"] | d.deskFocus;
  s.deskAway = doc["deskAway"] | d.deskAway;
  JsonArray layouts = doc["layouts"].as<JsonArray>();
  for (uint8_t i = 0; i < kLayoutCount; i++) {
    const uint8_t v = i < layouts.size() ? layouts[i].as<uint8_t>() : kDefaultLayouts[i];
    s.layouts[i] = v < 3 ? v : kDefaultLayouts[i];
  }
  return true;
}

}  // namespace

void settings_begin() {
  lock = xSemaphoreCreateMutex();
  Preferences prefs;
  prefs.begin("calendar", true);
  String json = prefs.getString("cfg", "");
  prefs.end();

  if (json.length() && fromJson(json, current)) return;
  // Flash was erased (e.g. a clean reflash): fall back to the SD card copy.
  String backup;
  if (storage_read(kBackupPath, backup) && fromJson(backup, current)) {
    settings_save(current);
  }
}

Settings settings_get() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Settings copy = current;
  xSemaphoreGive(lock);
  return copy;
}

void settings_save(const Settings &s) {
  xSemaphoreTake(lock, portMAX_DELAY);
  current = s;
  version++;
  dirty = true;
  changedAt = millis();
  xSemaphoreGive(lock);
}

void settings_flush(bool now) {
  xSemaphoreTake(lock, portMAX_DELAY);
  if (!dirty || (!now && millis() - changedAt < kSaveDelayMs)) {
    xSemaphoreGive(lock);
    return;
  }
  dirty = false;
  const String json = toJson(current);
  xSemaphoreGive(lock);

  Preferences prefs;
  prefs.begin("calendar", false);
  prefs.putString("cfg", json);
  prefs.end();
  storage_write(kBackupPath, json);
}

uint32_t settings_version() {
  return version;
}
