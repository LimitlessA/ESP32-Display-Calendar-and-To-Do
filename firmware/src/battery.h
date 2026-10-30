#pragma once

#include <Arduino.h>

// Optional MAX17048 fuel gauge on the I2C header. Call from the LVGL task
// only, since it shares the bus with the touch controller.
void battery_begin();
bool battery_present();
// Percent 0-100, or -1 if no gauge. Cached; refreshes at most every 30 s.
int battery_percent();
bool battery_charging();
