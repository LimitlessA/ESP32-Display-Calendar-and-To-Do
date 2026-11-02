#pragma once

#include <Arduino.h>

// Low power (Settings > Features > Low power): between Settings::sleepStart
// and sleepEnd (local time, and all day on weekends if chosen) the display
// sleeps with the backlight off and the panel stopped; a tap wakes it. After a
// tap it stays awake until nobody has touched it for a few minutes, then sleeps
// again. At the end of the window it wakes by itself.
//
// Light sleep keeps RAM, so it carries on where it was; Wi-Fi reconnects and
// a sync runs. Deep sleep restarts the board on wake. How a tap is noticed
// depends on TOUCH_INT_GPIO in config.h; see power.cpp.

// First thing in setup(): logs why the board woke. During a deep sleep it
// checks the touch controller and goes straight back to sleep if nobody
// touched it (this call then does not return).
void power_begin();

// From loop(): sleeps when it is time to.
void power_poll();

// Test harness: sleep now for this many seconds (or until a tap), in the
// configured mode, whatever the window and switch say.
void power_sleep_for(uint32_t seconds);
