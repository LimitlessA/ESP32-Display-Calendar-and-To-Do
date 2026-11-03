#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

#include "psram.h"
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

// The SD backup keeps secrets (Wi-Fi and desk passwords, account tokens, calendar links)
// encrypted with AES-256-CTR. The key comes from this chip's MAC address, so a copy of the
// card is useless on another board and passwords are not readable when the card is read in a
// PC. It is not protection against someone who has this board and the source code.
constexpr const char *kSealed = "enc1:";

void backupKey(uint8_t key[32]) {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  uint8_t in[22] = "calendar-backup-v1";
  memcpy(in + 16, mac, 6);
  mbedtls_sha256(in, sizeof(in), key, 0);
}

String seal(const String &plain) {
  if (!plain.length()) return plain;
  uint8_t key[32], nonce[16], block[16], iv[16];
  backupKey(key);
  esp_fill_random(iv, sizeof(iv));
  memcpy(nonce, iv, sizeof(nonce));
  const size_t n = plain.length();
  std::vector<uint8_t> raw(16 + n);
  memcpy(raw.data(), iv, 16);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 256);
  size_t off = 0;
  mbedtls_aes_crypt_ctr(&aes, n, &off, nonce, block, (const uint8_t *)plain.c_str(), raw.data() + 16);
  mbedtls_aes_free(&aes);
  std::vector<uint8_t> b64(4 * (raw.size() / 3 + 1) + 4);
  size_t olen = 0;
  mbedtls_base64_encode(b64.data(), b64.size(), &olen, raw.data(), raw.size());
  return String(kSealed) + String((const char *)b64.data()).substring(0, olen);
}

// Plain text (an older backup, or a value that was never sealed) comes back unchanged.
String unseal(const String &text) {
  if (!text.startsWith(kSealed)) return text;
  const String b64 = text.substring(strlen(kSealed));
  std::vector<uint8_t> raw(b64.length() + 4);
  size_t olen = 0;
  if (mbedtls_base64_decode(raw.data(), raw.size(), &olen, (const uint8_t *)b64.c_str(), b64.length()) != 0 ||
      olen < 16) {
    return "";
  }
  uint8_t key[32], nonce[16], block[16];
  backupKey(key);
  memcpy(nonce, raw.data(), 16);
  const size_t n = olen - 16;
  std::vector<uint8_t> out(n + 1, 0);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 256);
  size_t off = 0;
  mbedtls_aes_crypt_ctr(&aes, n, &off, nonce, block, raw.data() + 16, out.data());
  mbedtls_aes_free(&aes);
  return String((const char *)out.data());
}

String toJson(const Settings &s, bool backup = false) {
  JsonDocument doc(psram_json());
  doc["wifiSsid"] = s.wifiSsid;
  doc["wifiPassword"] = backup ? seal(s.wifiPassword) : s.wifiPassword;
  JsonArray accounts = doc["accounts"].to<JsonArray>();
  for (const Account &a : s.accounts) {
    JsonObject o = accounts.add<JsonObject>();
    o["label"] = a.label;
    o["url"] = a.url;
    o["token"] = backup ? seal(a.token) : a.token;
    o["color"] = a.color;
  }
  JsonArray ics = doc["ics"].to<JsonArray>();
  for (const IcsCalendar &c : s.ics) {
    JsonObject o = ics.add<JsonObject>();
    o["label"] = c.label;
    o["url"] = backup ? seal(c.url) : c.url;
    o["color"] = c.color;
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
  doc["dimLevel"] = s.dimLevel;
  doc["use24h"] = s.use24h;
  doc["syncMinutes"] = s.refreshMinutes;
  doc["theme"] = s.theme;
  doc["updateUrl"] = s.updateUrl;
  doc["timezone"] = s.timezone;
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
  doc["deskIp"] = s.deskIp;
  doc["deskUser"] = s.deskUser;
  doc["deskPassword"] = backup ? seal(s.deskPassword) : s.deskPassword;
  doc["deskSitMin"] = s.deskSitMin;
  doc["deskStandMin"] = s.deskStandMin;
  doc["deskSitSlot"] = s.deskSitSlot;
  for (int i = 0; i < 4; i++) doc["deskLabel" + String(i + 1)] = s.deskLabel[i];
  doc["deskStandSlot"] = s.deskStandSlot;
  doc["deskMeetings"] = s.deskMeetings;
  doc["deskFocus"] = s.deskFocus;
  doc["deskAway"] = s.deskAway;
  doc["lowPower"] = s.lowPower;
  doc["sleepStart"] = s.sleepStart;
  doc["sleepEnd"] = s.sleepEnd;
  doc["sleepWeekends"] = s.sleepWeekends;
  doc["sleepMode"] = s.sleepMode;
  doc["sleepIdleMinutes"] = s.sleepIdleMinutes;
  JsonArray layouts = doc["layouts"].to<JsonArray>();
  for (uint8_t v : s.layouts) layouts.add(v);
  String out;
  serializeJson(doc, out);
  return out;
}

bool fromJson(const String &json, Settings &s) {
  JsonDocument doc(psram_json());
  if (deserializeJson(doc, json)) return false;
  Settings d;  // defaults for anything missing
  s.wifiSsid = doc["wifiSsid"] | "";
  s.wifiPassword = unseal(String(doc["wifiPassword"] | ""));
  s.accounts.clear();
  for (JsonObject o : doc["accounts"].as<JsonArray>()) {
    s.accounts.push_back({o["label"] | "", o["url"] | "", unseal(String(o["token"] | "")), o["color"] | 0x6C8CFFu});
  }
  s.ics.clear();
  for (JsonObject o : doc["ics"].as<JsonArray>()) {
    if (s.ics.size() < kMaxIcs) s.ics.push_back({o["label"] | "", unseal(String(o["url"] | "")), o["color"] | 0x4FD1C5u});
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
  s.dimLevel = doc["dimLevel"] | d.dimLevel;
  s.use24h = doc["use24h"] | d.use24h;
  // "syncMinutes" replaces "refreshMinutes", which had no control and stayed at
  // the old 5-minute default; dropping it moves those displays to the new one.
  s.refreshMinutes = max<uint8_t>(1, doc["syncMinutes"] | d.refreshMinutes);
  s.theme = doc["theme"] | d.theme;
  s.updateUrl = doc["updateUrl"] | d.updateUrl;
  s.timezone = doc["timezone"] | d.timezone;
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
  s.deskIp = doc["deskIp"] | "";
  s.deskUser = doc["deskUser"] | d.deskUser;
  s.deskPassword = unseal(String(doc["deskPassword"] | d.deskPassword.c_str()));
  s.deskSitMin = doc["deskSitMin"] | d.deskSitMin;
  s.deskStandMin = doc["deskStandMin"] | d.deskStandMin;
  for (int i = 0; i < 4; i++) {
    s.deskLabel[i] = String((const char *)(doc["deskLabel" + String(i + 1)] | ""));
    s.deskLabel[i].trim();
    s.deskLabel[i] = s.deskLabel[i].substring(0, 16);
  }
  s.deskSitSlot = constrain(doc["deskSitSlot"] | d.deskSitSlot, 1, 4);
  s.deskStandSlot = constrain(doc["deskStandSlot"] | d.deskStandSlot, 1, 4);
  s.deskMeetings = doc["deskMeetings"] | d.deskMeetings;
  s.deskFocus = doc["deskFocus"] | d.deskFocus;
  s.deskAway = doc["deskAway"] | d.deskAway;
  s.lowPower = doc["lowPower"] | d.lowPower;
  s.sleepStart = (doc["sleepStart"] | d.sleepStart) % 1440;
  s.sleepEnd = (doc["sleepEnd"] | d.sleepEnd) % 1440;
  s.sleepWeekends = doc["sleepWeekends"] | d.sleepWeekends;
  s.sleepMode = (doc["sleepMode"] | d.sleepMode) ? 1 : 0;
  s.sleepIdleMinutes = constrain(doc["sleepIdleMinutes"] | d.sleepIdleMinutes, 1, 60);
  JsonArray layouts = doc["layouts"].as<JsonArray>();
  for (uint8_t i = 0; i < kLayoutCount; i++) {
    const uint8_t v = i < layouts.size() ? layouts[i].as<uint8_t>() : kDefaultLayouts[i];
    s.layouts[i] = v < layout_variants(i) ? v : kDefaultLayouts[i];
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
  const Settings snapshot = current;
  const String json = toJson(snapshot);
  xSemaphoreGive(lock);

  Preferences prefs;
  prefs.begin("calendar", false);
  prefs.putString("cfg", json);
  prefs.end();
  storage_write(kBackupPath, toJson(snapshot, true));
}

uint32_t settings_version() {
  return version;
}
