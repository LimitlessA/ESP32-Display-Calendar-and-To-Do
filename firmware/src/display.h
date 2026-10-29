#pragma once

// Brings up the 800x480 RGB panel, backlight and GT911 touch, and registers
// them with LVGL. Call once from setup(), then lv_timer_handler() in loop().
void display_init();

void display_set_brightness(uint8_t level);
