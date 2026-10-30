#include "presence.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <mbedtls/aes.h>

#include <array>
#include <vector>

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kName = "Calendar display";
constexpr uint32_t kPairWindowMs = 120000;  // pairing closes by itself after this
constexpr uint16_t kScanInterval = 160;     // 100 ms, in 0.625 ms units
constexpr uint16_t kScanWindow = 48;        // 30 ms of every 100 ms: leaves room for Wi-Fi
constexpr uint16_t kAppearanceHid = 0x03C0;

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

bool started = false;
bool enabled = false;
volatile bool pairing = false;
bool bondingOpen = false;  // NimBLE currently accepts new bonds
volatile uint32_t pairUntil = 0;
volatile uint32_t lastSeenMs = 0;
volatile bool everSeen = false;
volatile int connected = 0;
std::vector<std::array<uint8_t, 16>> irks;  // identity keys of paired phones, reversed for AES
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

// ---- Code --------------------------------------------------------------------

void seen() {
  lastSeenMs = millis();
  everSeen = true;
}

// Reads the identity resolving keys of every bonded phone from NimBLE's store.
void loadIrks() {
  std::vector<std::array<uint8_t, 16>> found;
  for (int i = 0; i < NimBLEDevice::getNumBonds(); i++) {
    const NimBLEAddress addr = NimBLEDevice::getBondedAddress(i);
    struct ble_store_key_sec key = {};
    key.peer_addr.type = addr.getType();
    memcpy(key.peer_addr.val, addr.getNative(), 6);
    struct ble_store_value_sec value;
    if (ble_store_read_peer_sec(&key, &value) == 0 && value.irk_present) {
      std::array<uint8_t, 16> k;
      for (int b = 0; b < 16; b++) k[b] = value.irk[15 - b];  // NimBLE keeps keys little-endian
      found.push_back(k);
    }
  }
  portENTER_CRITICAL(&lock);
  irks.swap(found);
  portEXIT_CRITICAL(&lock);
}

// Resolvable private address check from the Bluetooth spec: ah(IRK, prand) == hash.
bool resolves(const uint8_t *addr) {
  if ((addr[5] & 0xC0) != 0x40) return false;  // not a resolvable private address
  uint8_t in[16] = {0}, out[16];
  in[13] = addr[5];
  in[14] = addr[4];
  in[15] = addr[3];
  std::vector<std::array<uint8_t, 16>> keys;
  portENTER_CRITICAL(&lock);
  keys = irks;
  portEXIT_CRITICAL(&lock);
  for (const auto &k : keys) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, k.data(), 128);
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, in, out);
    mbedtls_aes_free(&aes);
    if (out[15] == addr[0] && out[14] == addr[1] && out[13] == addr[2]) return true;
  }
  return false;
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
  loadIrks();
  started = true;
}

}  // namespace

void presence_apply(bool on) {
  if (on == enabled) return;
  enabled = on;
  if (on) {
    if (!started) begin();
    NimBLEDevice::startAdvertising();
    NimBLEDevice::getScan()->start(0, nullptr, false);
  } else if (started) {
    NimBLEDevice::getScan()->stop();
    NimBLEDevice::stopAdvertising();
    NimBLEServer *server = NimBLEDevice::getServer();
    for (uint16_t h : server->getPeerDevices()) server->disconnect(h);
    pairing = false;
  }
}

PhoneState presence_state(uint8_t awayMinutes) {
  if (!enabled) return PhoneState::Off;
  if (pairing && millis() - pairUntil < 0x80000000UL) pairing = false;  // window over
  if (!pairing && bondingOpen) {
    bondingOpen = false;
    NimBLEDevice::setSecurityAuth(false, false, true);
  }
  if (connected > 0) seen();
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
  bondingOpen = open;
  NimBLEDevice::setSecurityAuth(open, false, true);  // bond, no PIN (the display has no keypad), secure connections
  NimBLEDevice::startAdvertising();
}

bool presence_pairing() { return pairing; }

int presence_paired_count() { return started ? NimBLEDevice::getNumBonds() : 0; }

void presence_forget() {
  if (!started) return;
  NimBLEDevice::deleteAllBonds();
  loadIrks();
  everSeen = false;
}
