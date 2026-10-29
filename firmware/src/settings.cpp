#include <ArduinoJson.h>
#include <Preferences.h>

#include "settings.h"
#include "storage.h"

namespace {

constexpr const char *kBackupPath = "/calendar/settings.json";

SemaphoreHandle_t lock;
Settings current;
uint32_t version = 1;

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
  s.updateUrl = doc["updateUrl"] | "";
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
  const String json = toJson(s);
  xSemaphoreTake(lock, portMAX_DELAY);
  current = s;
  version++;
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
