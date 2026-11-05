#include "presence.h"
#include "settings.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <esp_heap_caps.h>
#include <nvs.h>
#include <mbedtls/aes.h>

#include <vector>

// nimconfig.h picks INTERNAL unless sdkconfig.h sets EXTERNAL.
#ifdef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL
#warning "NimBLE keeps its buffers in internal RAM: check custom_sdkconfig"
#endif

// The Arduino core frees the Bluetooth controller's memory (about 36 KB of
// internal RAM) at start-up unless bleInUse() says otherwise; NimBLE-Arduino
// does not tell it, so turning presence on crashed the display in
// esp_bt_controller_init. Keep the memory only when presence is switched on in
// the saved settings (NVS is ready by the time the core asks).
static bool bleMemoryKept = false;

extern "C" bool bleInUse(void) {
  nvs_handle_t h;
  if (nvs_open("calendar", NVS_READONLY, &h) != ESP_OK) return false;
  size_t len = 0;
  if (nvs_get_str(h, "cfg", nullptr, &len) == ESP_OK && len > 0 && len < 16384) {
    char *json = (char *)malloc(len);
    if (json && nvs_get_str(h, "cfg", json, &len) == ESP_OK) {
      bleMemoryKept = strstr(json, "\"presence\":true") != nullptr;
    }
    free(json);
  }
  nvs_close(h);
  return bleMemoryKept;
}

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kName = "Calendar display";
constexpr uint32_t kPairWindowMs = 120000;  // pairing closes by itself after this
constexpr uint16_t kScanInterval = 160;     // 100 ms, in 0.625 ms units
constexpr uint16_t kScanWindow = 48;        // 30 ms of every 100 ms: leaves room for Wi-Fi
constexpr uint16_t kAppearanceHid = 0x03C0;
// Bluetooth starts on its own task: NimBLE's start-up waits for the radio
// and needs more stack than the UI loop can spare, and must not stall it.
constexpr uint32_t kStartStack = 6144;
// The Bluetooth controller and NimBLE's tasks need internal RAM, which Wi-Fi
// and the display's buffers share. NimBLE stops the program (the display
// restarts) if it runs out while starting, so check first. Includes the
// start task's stack, which is freed again afterwards. The controller's own
// memory (about 36 KB) is kept out of the heap at start-up (bleInUse()), so
// this only covers NimBLE's tasks and buffers and leaves Wi-Fi and TLS some
// room. The harness "heap" command shows what Bluetooth really took
// (presence_ram_used()). Expected on the 7" with presence on: about 50-70 KB
// free before it starts, now that the task stacks are in PSRAM.
constexpr size_t kNeedInternal = 28 * 1024;  // heap left with the controller memory already kept
constexpr size_t kNeedBlock = 16 * 1024;
constexpr const char *kErrMemory = "Bluetooth could not start: only %u KB of internal memory free";
constexpr const char *kErrTask = "Bluetooth could not start";
constexpr const char *kRestarting = "Restarting to start Bluetooth...";
constexpr uint32_t kRestartDelayMs = 2500;
uint32_t restartAt = 0;

// Just a media remote with no keys: enough for phones to list it in their
// Bluetooth settings and reconnect to it, without acting as a keyboard.
const uint8_t kReportMap[] = {
    0x05, 0x0C,  // Usage Page (Consumer)
    0x09, 0x01,  // Usage (Consumer Control)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID (1)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x09, 0xE9,  //   Usage (Volume Up), unused
    0x81, 0x02,  //   Input (Data, Var, Abs)
    0xC0,        // End Collection
};

// ---- State -------------------------------------------------------------------

enum class Bt : uint8_t { Off, Starting, Ready, Failed };
volatile Bt bt = Bt::Off;  // written by the start task, read by the UI
bool running = false;      // advertising and scanning
bool enabled = false;
char errorText[96] = "";
volatile bool pairing = false;
bool bondingOpen = false;  // NimBLE currently accepts new bonds
volatile uint32_t pairUntil = 0;
volatile uint32_t lastSeenMs = 0;
volatile bool everSeen = false;
volatile int connected = 0;
// Identity keys of paired phones, reversed for AES. Fixed size, no heap: the
// scan checks them for every advertisement it hears (many per second).
constexpr uint8_t kMaxIrks = 4;
uint8_t irks[kMaxIrks][16];
uint8_t irkCount = 0;
volatile uint32_t irkGen = 0;  // bumped when the keys change; the scan then forgets its cache

// Guards for Wi-Fi: below this much free internal RAM the scan pauses (Wi-Fi
// needs it to stay joined), and resumes above the second value.
constexpr size_t kScanPauseBelow = 20 * 1024;
constexpr size_t kScanResumeAbove = 30 * 1024;
bool scanPausedForRam = false;
uint32_t scanRestarts = 0;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

// ---- Code --------------------------------------------------------------------

void seen() {
  lastSeenMs = millis();
  everSeen = true;
}

// Reads the identity resolving keys of every bonded phone from NimBLE's store.
void loadIrks() {
  uint8_t found[kMaxIrks][16];
  uint8_t n = 0;
  for (int i = 0; i < NimBLEDevice::getNumBonds() && n < kMaxIrks; i++) {
    const NimBLEAddress addr = NimBLEDevice::getBondedAddress(i);
    struct ble_store_key_sec key = {};
    key.peer_addr.type = addr.getType();
    memcpy(key.peer_addr.val, addr.getNative(), 6);
    struct ble_store_value_sec value;
    if (ble_store_read_peer_sec(&key, &value) == 0 && value.irk_present) {
      for (int b = 0; b < 16; b++) found[n][b] = value.irk[15 - b];  // NimBLE keeps keys little-endian
      n++;
    }
  }
  portENTER_CRITICAL(&lock);
  memcpy(irks, found, sizeof(irks));
  irkCount = n;
  irkGen++;
  portEXIT_CRITICAL(&lock);
}

// Resolvable private address check from the Bluetooth spec: ah(IRK, prand) == hash.
// Runs on NimBLE's host task only. A phone repeats its address in every
// advertisement until it changes it (every ~15 min), so the last answers are
// cached and each address costs one AES pass per key, once. No heap use.
struct Seen {
  uint8_t addr[6];
  bool match;
};
constexpr uint8_t kCache = 32;
Seen cache[kCache];
uint8_t cacheNext = 0, cacheCount = 0;
uint32_t cacheGen = UINT32_MAX;

bool resolves(const uint8_t *addr) {
  if ((addr[5] & 0xC0) != 0x40) return false;  // not a resolvable private address
  uint8_t keys[kMaxIrks][16];
  uint8_t n;
  uint32_t gen;
  portENTER_CRITICAL(&lock);
  n = irkCount;
  gen = irkGen;
  memcpy(keys, irks, sizeof(keys));
  portEXIT_CRITICAL(&lock);
  if (!n) return false;
  if (gen != cacheGen) {
    cacheGen = gen;
    cacheCount = 0;
    cacheNext = 0;
  }
  for (uint8_t i = 0; i < cacheCount; i++) {
    if (!memcmp(cache[i].addr, addr, 6)) return cache[i].match;
  }
  uint8_t in[16] = {0}, out[16];
  in[13] = addr[5];
  in[14] = addr[4];
  in[15] = addr[3];
  bool match = false;
  for (uint8_t k = 0; k < n && !match; k++) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    if (mbedtls_aes_setkey_enc(&aes, keys[k], 128) == 0 &&
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, in, out) == 0) {
      match = out[15] == addr[0] && out[14] == addr[1] && out[13] == addr[2];
    }
    mbedtls_aes_free(&aes);
  }
  memcpy(cache[cacheNext].addr, addr, 6);
  cache[cacheNext].match = match;
  cacheNext = (cacheNext + 1) % kCache;
  if (cacheCount < kCache) cacheCount++;
  return match;
}

class ServerEvents : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, ble_gap_conn_desc *) override {
    connected++;
    NimBLEDevice::startAdvertising();  // stay visible for a second phone
  }
  void onDisconnect(NimBLEServer *, ble_gap_conn_desc *) override {
    if (connected > 0) connected--;
    seen();  // it was here a moment ago
  }
  void onAuthenticationComplete(ble_gap_conn_desc *desc) override {
    if (!desc->sec_state.encrypted || !desc->sec_state.bonded) {
      NimBLEDevice::getServer()->disconnect(desc->conn_handle);
      return;
    }
    seen();
    loadIrks();
    if (pairing) pairing = false;  // paired: close the window
  }
};

class ScanEvents : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *dev) override {
    const NimBLEAddress addr = dev->getAddress();
    if (addr.getType() == BLE_ADDR_RANDOM && resolves(addr.getNative())) seen();
  }
};

void begin() {
  NimBLEDevice::init(kName);
  NimBLEDevice::setSecurityAuth(false, false, true);  // no new bonds until a pairing window opens
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerEvents());
  server->advertiseOnDisconnect(true);
  NimBLEHIDDevice *hid = new NimBLEHIDDevice(server);
  hid->manufacturer()->setValue("ESP32 calendar");
  hid->pnp(0x02, 0x303A, 0x0001, 0x0100);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap((uint8_t *)kReportMap, sizeof(kReportMap));
  hid->inputReport(1);
  hid->startServices();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(kAppearanceHid);
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->setScanResponse(true);

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanEvents(), true);
  scan->setActiveScan(false);
  scan->setInterval(kScanInterval);
  scan->setWindow(kScanWindow);
  // Keep no list of what was seen. NimBLE otherwise stores every advertiser
  // forever during an endless scan (phones and tags change address every
  // few minutes), in internal RAM: after a few hours Wi-Fi had none left
  // to join the router ("esp-aes: Failed to allocate memory", 2026-11-05).
  scan->setMaxResults(0);
  loadIrks();
}

size_t ramBefore = 0, ramUsed = 0;  // internal RAM free before Bluetooth started, and what it took

void startTask(void *) {
  begin();
  // What stays taken once this task's own stack is freed again.
  const size_t now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) + kStartStack;
  ramUsed = ramBefore > now ? ramBefore - now : 0;
  bt = Bt::Ready;
  vTaskDelete(nullptr);
}

void startBluetooth() {
  if (!bleMemoryKept) {
    // Its memory was given away at start-up: save and restart with it kept.
    snprintf(errorText, sizeof(errorText), "%s", kRestarting);
    bt = Bt::Failed;
    settings_flush(true);
    restartAt = millis() + kRestartDelayMs;
    return;
  }
  const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  log_i("presence: %u bytes internal free, largest block %u", (unsigned)freeInternal, (unsigned)block);
  if (freeInternal < kNeedInternal || block < kNeedBlock) {
    snprintf(errorText, sizeof(errorText), kErrMemory, (unsigned)(freeInternal / 1024));
    log_w("%s", errorText);
    bt = Bt::Failed;
    return;
  }
  bt = Bt::Starting;
  ramBefore = freeInternal;
  if (xTaskCreatePinnedToCore(startTask, "ble start", kStartStack, nullptr, 1, nullptr, 0) != pdPASS) {
    snprintf(errorText, sizeof(errorText), "%s", kErrTask);
    bt = Bt::Failed;
  }
}

void setBonding(bool open) {
  bondingOpen = open;
  NimBLEDevice::setSecurityAuth(open, false, true);  // bond, no PIN (the display has no keypad), secure connections
}


// Once a second while running: Wi-Fi first. The scan pauses while internal
// RAM is low, and starts again if it stopped by itself (NimBLE ends it on a
// host reset or some connection events). Advertising too, so the phone can
// always reconnect.
void keepScanning() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  const size_t freeRam = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (!scanPausedForRam && freeRam < kScanPauseBelow) {
    scanPausedForRam = true;
    scan->stop();
    scan->clearResults();
    log_w("presence: scan paused, only %u bytes internal RAM free", (unsigned)freeRam);
    return;
  }
  if (scanPausedForRam) {
    if (freeRam < kScanResumeAbove) return;
    scanPausedForRam = false;
    log_i("presence: scan resumed, %u bytes internal RAM free", (unsigned)freeRam);
  }
  if (!scan->isScanning()) {
    scanRestarts++;
    scan->clearResults();
    scan->start(0, nullptr, false);
  }
  if (!NimBLEDevice::getAdvertising()->isAdvertising() && connected < 2) NimBLEDevice::startAdvertising();
}

}  // namespace

// Called every second: Bluetooth comes up in the background, and
// advertising and scanning start once it is ready.
void presence_apply(bool on) {
  enabled = on;
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) {
    if (on) ESP.restart();
    restartAt = 0;  // turned off again before the restart
  }
  if (on) {
    if (bt == Bt::Off) startBluetooth();
    if (bt == Bt::Ready && !running) {
      running = true;
      if (pairing) setBonding(true);  // the pairing window opened while starting
      NimBLEDevice::startAdvertising();
      NimBLEDevice::getScan()->start(0, nullptr, false);
    }
    if (bt == Bt::Ready && running) keepScanning();
    return;
  }
  pairing = false;
  if (bt == Bt::Failed) {
    bt = Bt::Off;  // try again when it is turned back on
    errorText[0] = 0;
  }
  if (running) {
    running = false;
    NimBLEDevice::getScan()->stop();
    NimBLEDevice::stopAdvertising();
    NimBLEServer *server = NimBLEDevice::getServer();
    for (uint16_t h : server->getPeerDevices()) server->disconnect(h);
  }
}

const char *presence_error() { return bt == Bt::Failed ? errorText : nullptr; }

bool presence_ready() { return bt == Bt::Ready; }

size_t presence_ram_used() { return ramUsed; }

PhoneState presence_state(uint8_t awayMinutes) {
  if (!enabled) return PhoneState::Off;
  if (pairing && millis() - pairUntil < 0x80000000UL) pairing = false;  // window over
  if (!pairing && bondingOpen) setBonding(false);
  if (connected > 0) seen();
  if (scanPausedForRam) seen();  // not scanning is no reason to call the phone away
  if (!presence_paired_count()) return PhoneState::NotPaired;
  if (!everSeen) return PhoneState::Away;
  return millis() - lastSeenMs < awayMinutes * 60000UL ? PhoneState::Near : PhoneState::Away;
}

long presence_seen_ago() {
  if (!everSeen) return -1;
  return (long)((millis() - lastSeenMs) / 1000);
}

// Outside the window bonding is off, so a new phone only gets an unbonded
// link, which onAuthenticationComplete drops.
void presence_pair(bool open) {
  if (!enabled) return;
  pairUntil = millis() + kPairWindowMs;
  pairing = open;
  if (!running) return;  // presence_apply opens it once Bluetooth is up
  setBonding(open);
  NimBLEDevice::startAdvertising();
}

bool presence_pairing() { return pairing; }

int presence_paired_count() { return bt == Bt::Ready ? NimBLEDevice::getNumBonds() : 0; }

void presence_forget() {
  if (bt != Bt::Ready) return;
  NimBLEDevice::deleteAllBonds();
  loadIrks();
  everSeen = false;
}
