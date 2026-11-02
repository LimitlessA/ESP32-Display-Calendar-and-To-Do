#pragma once

// Brings up the 800x480 RGB panel, backlight and GT911 touch, and registers
// them with LVGL. Call once from setup(), then lv_timer_handler() in loop().
void display_init();

void display_set_brightness(uint8_t level);

// Turns the backlight off (true) or back to the last brightness (false).
// Used while firmware is written to flash: every flash write pauses the
// picture feed, so the screen would flicker and jump the whole time.
void display_blank(bool blank);

// The picture on screen: RGB565, w x h pixels, row by row (for screenshots).
const uint16_t *display_frame(int *w, int *h);

// Low power (power.cpp). display_sleep(true) turns the backlight off and stops
// the panel; display_sleep(false) starts it again and redraws, or returns false
// if it could not (then restart). display_touched() reads the touch controller
// once over I2C; before display_init() it starts the touch driver alone (the
// deep-sleep boot check). display_park() holds the backlight off in deep sleep.
bool display_sleep(bool sleep);
bool display_touched();
void display_park();
