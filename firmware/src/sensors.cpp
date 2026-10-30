#include <LovyanGFX.hpp>

#include "config.h"
#include "sensors.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr uint8_t kContinuousHighRes = 0x10;  // BH1750: 1 lx resolution, ~120 ms per reading
constexpr uint32_t kFreq = 400000;
constexpr uint32_t kReadEveryMs = 1000;
constexpr float kLuxPerCount = 1.0f / 1.2f;  // from the BH1750 datasheet

// ---- State -------------------------------------------------------------------

bool lightFound = false;
int lux = -1;
uint32_t lastRead = 0;
bool motionWasHigh = false;

// ---- Code --------------------------------------------------------------------

bool readLight() {
  uint8_t buf[2];
  if (!lgfx::i2c::transactionRead(I2C_PORT, LIGHT_SENSOR_ADDR, buf, 2, kFreq).has_value()) return false;
  lux = int(((buf[0] << 8) | buf[1]) * kLuxPerCount);
  lastRead = millis();
  return true;
}

}  // namespace

void sensors_begin() {
  const uint8_t cmd = kContinuousHighRes;
  lightFound = lgfx::i2c::transactionWrite(I2C_PORT, LIGHT_SENSOR_ADDR, &cmd, 1, kFreq).has_value();
  log_i("light sensor %s", lightFound ? "found" : "not present");
  pinMode(MOTION_PIN, INPUT_PULLDOWN);
}

bool light_present() {
  return lightFound;
}

int light_lux() {
  if (!lightFound) return -1;
  if (millis() - lastRead >= kReadEveryMs) readLight();
  return lux;
}

bool motion_seen() {
  const bool high = digitalRead(MOTION_PIN) == HIGH;
  const bool rising = high && !motionWasHigh;
  motionWasHigh = high;
  return rising;
}
