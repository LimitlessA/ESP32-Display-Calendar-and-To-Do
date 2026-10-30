#pragma once

#include "model.h"

// Builds every screen. Call after display_init().
void ui_init();

// Called from loop() whenever sync has new data.
void ui_update(const Snapshot &snap);

// Called from loop() a few times a second: clock, reminders, night mode, dimming.
void ui_tick();

// Backlight control, provided by the platform (display.cpp or the simulator).
void ui_set_backlight(uint8_t percent);
