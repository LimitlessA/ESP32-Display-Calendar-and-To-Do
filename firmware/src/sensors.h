#pragma once

#include <Arduino.h>

// Optional room sensors. Call from the LVGL task only: the light sensor shares
// the I2C bus with the touch controller.
void sensors_begin();

// BH1750 light sensor on the I2C header.
bool light_present();
// Room light in lux, or -1 without a sensor. Cached; reads at most once a second.
int light_lux();

// PIR sensor on MOTION_PIN. True once for every new movement.
bool motion_seen();
