#pragma once

// Test harness: drive and inspect the display from a PC, over the serial port
// (115200, one command per line) or the setup page (/harness?pin=..&cmd=..,
// /shot.bmp?pin=..). tools/harness.py is the PC side. Commands:
//   tap X Y | press X Y MS | swipe X1 Y1 X2 Y2 [MS]   touch input for LVGL
//   screen NAME                                        home, week, tasks, ...
//   shot                                               screenshot (serial: RLE+base64)
//   heap | tasks (free stack per task) | crash (why it last restarted)
//   log | help | pin (serial only: the setup page PIN and address)
// Built in unless HARNESS=0.

#include <Arduino.h>

#ifndef HARNESS
#define HARNESS 1
#endif

#include <WebServer.h>

// Call once after the display is up, then from loop().
void harness_begin();
void harness_poll();

// The touch the harness is pressing right now, if any (called by the touch driver).
bool harness_touch(int16_t *x, int16_t *y);

// Runs one command on the UI task and returns its reply (for the web server).
String harness_run(const String &cmd, uint32_t timeout_ms = 3000);

// Streams the screen as a BMP (for the web server).
void harness_send_bmp(WebServer &server);
