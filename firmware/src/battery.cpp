#include <LovyanGFX.hpp>

#include "battery.h"
#include "config.h"

namespace {

constexpr uint8_t kRegSoc = 0x04;    // state of charge, high byte = percent
constexpr uint8_t kRegCrate = 0x16;  // charge rate, signed, 0.208 %/h per bit
constexpr uint32_t kFreq = 400000;
constexpr uint32_t kReadEveryMs = 30000;  // the level changes slowly

bool present = false;
int percent = -1;
bool charging = false;
uint32_t lastRead = 0;

bool readWord(uint8_t reg, uint16_t &out) {
  uint8_t buf[2];
  if (!lgfx::i2c::readRegister(I2C_PORT, FUEL_GAUGE_ADDR, reg, buf, 2, kFreq).has_value()) return false;
  out = (buf[0] << 8) | buf[1];
  return true;
}

void refresh() {
  uint16_t soc, crate;
  if (!readWord(kRegSoc, soc)) return;
  percent = min(100, soc >> 8);
  charging = readWord(kRegCrate, crate) && int16_t(crate) > 0;
  lastRead = millis();
}

}  // namespace

void battery_begin() {
  uint16_t soc;
  present = readWord(kRegSoc, soc);
  if (present) refresh();
  log_i("fuel gauge %s", present ? "found" : "not present");
}

bool battery_present() {
  return present;
}

int battery_percent() {
  if (!present) return -1;
  if (millis() - lastRead > kReadEveryMs) refresh();
  return percent;
}

bool battery_charging() {
  return present && charging;
}
