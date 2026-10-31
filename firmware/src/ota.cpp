#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Update.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "ota.h"
#include "ota_root_cas.h"
#include "settings.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kErrNoLink = "No update link set. Upload a file at http://" HOSTNAME ".local";
constexpr const char *kErrRead = "Could not read the update link";
constexpr const char *kErrManifest = "Update link is not a valid manifest";
constexpr const char *kErrCheckFirst = "Check for updates first";
constexpr uint32_t kTimeoutMs = 15000;
constexpr uint32_t kRestartDelayMs = 2500;  // lets the screen show "Restarting"
constexpr uint32_t kTaskStack = 12288;

// ---- State -------------------------------------------------------------------

SemaphoreHandle_t lock;
TaskHandle_t task;
OtaStatus status;
String binUrl;
size_t uploadSize = 0;
size_t uploadDone = 0;

// ---- Code --------------------------------------------------------------------

enum : uint32_t { kCheck = 1, kInstall = 2, kReboot = 4 };

void set(OtaState st, const String &message = "", int progress = -1) {
  xSemaphoreTake(lock, portMAX_DELAY);
  status.state = st;
  status.message = message;
  if (progress >= 0) status.progress = progress;
  xSemaphoreGive(lock);
}

// "1.10.0" > "1.9.3": compare numerically, part by part.
bool newer(const String &a, const String &b) {
  int ia = 0, ib = 0;
  while (ia < (int)a.length() || ib < (int)b.length()) {
    const long na = a.substring(ia).toInt(), nb = b.substring(ib).toInt();
    if (na != nb) return na > nb;
    ia = a.indexOf('.', ia);
    ib = b.indexOf('.', ib);
    if (ia < 0 && ib < 0) return false;
    ia = ia < 0 ? a.length() : ia + 1;
    ib = ib < 0 ? b.length() : ib + 1;
  }
  return false;
}

void check() {
  const String url = settings_get().updateUrl;
  if (!url.length()) {
    set(OtaState::Failed, kErrNoLink);
    return;
  }
  set(OtaState::Checking);
  WiFiClientSecure client;
  client.setCACert(OTA_ROOT_CAS);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(kTimeoutMs);
  if (!http.begin(client, url) || http.GET() != HTTP_CODE_OK) {
    set(OtaState::Failed, kErrRead);
    http.end();
    return;
  }
  JsonDocument doc;
  const bool ok = !deserializeJson(doc, http.getString());
  http.end();
  if (!ok || !doc["version"].is<const char *>() || !doc["url"].is<const char *>()) {
    set(OtaState::Failed, kErrManifest);
    return;
  }
  const String latest = doc["version"].as<String>();
  xSemaphoreTake(lock, portMAX_DELAY);
  status.latest = latest;
  binUrl = doc["url"].as<String>();
  xSemaphoreGive(lock);
  set(newer(latest, FIRMWARE_VERSION) ? OtaState::Available : OtaState::UpToDate);
}

void install() {
  String url;
  xSemaphoreTake(lock, portMAX_DELAY);
  url = binUrl;
  xSemaphoreGive(lock);
  if (!url.length()) {
    set(OtaState::Failed, kErrCheckFirst);
    return;
  }
  set(OtaState::Installing, "", 0);
  WiFiClientSecure client;
  client.setCACert(OTA_ROOT_CAS);
  HTTPUpdate updater;
  updater.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  updater.rebootOnUpdate(false);
  updater.onProgress([](int done, int total) {
    if (total > 0) set(OtaState::Installing, "", done * 100 / total);
  });
  const HTTPUpdateResult r = updater.update(client, url, FIRMWARE_VERSION);
  if (r == HTTP_UPDATE_OK) {
    set(OtaState::Done, "", 100);
    xTaskNotify(task, kReboot, eSetBits);
  } else {
    set(OtaState::Failed, updater.getLastErrorString());
  }
}

void otaTask(void *) {
  for (;;) {
    uint32_t bits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);
    if (bits & kCheck) check();
    if (bits & kInstall) install();
    if (bits & kReboot) {
      delay(kRestartDelayMs);
      settings_flush(true);
      ESP.restart();
    }
  }
}

}  // namespace

void ota_begin() {
  lock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(otaTask, "ota", kTaskStack, nullptr, 1, &task, 0);
}

OtaStatus ota_status() {
  xSemaphoreTake(lock, portMAX_DELAY);
  OtaStatus copy = status;
  xSemaphoreGive(lock);
  return copy;
}

void ota_check() {
  xTaskNotify(task, kCheck, eSetBits);
}

void ota_install() {
  xTaskNotify(task, kInstall, eSetBits);
}

bool ota_upload_start(size_t size) {
  uploadSize = size;
  uploadDone = 0;
  set(OtaState::Installing, "", 0);
  if (!Update.begin(size ? size : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    set(OtaState::Failed, Update.errorString());
    return false;
  }
  return true;
}

bool ota_upload_write(const uint8_t *data, size_t len) {
  if (Update.write(const_cast<uint8_t *>(data), len) != len) {
    set(OtaState::Failed, Update.errorString());
    return false;
  }
  uploadDone += len;
  if (uploadSize) set(OtaState::Installing, "", uploadDone * 100 / uploadSize);
  return true;
}

bool ota_upload_finish() {
  if (!Update.end(true)) {
    set(OtaState::Failed, Update.errorString());
    return false;
  }
  set(OtaState::Done, "", 100);
  xTaskNotify(task, kReboot, eSetBits);
  return true;
}

void ota_upload_abort(const String &why) {
  Update.abort();
  set(OtaState::Failed, why);
}
