#pragma once

#include <Arduino.h>

// Why the display last restarted. A crash records its reason, task and
// backtrace in RTC memory (kept over a restart, lost without power), and the
// lowest free internal RAM is noted every few seconds. On the next start it
// is printed on the serial port, shown in Settings and by the harness
// ("crash"). Decode the addresses with
//   xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/<env>/firmware.elf ADDR...

// Call first thing in setup(), after Serial.begin().
void crashlog_begin();
// Call from loop(): notes the free internal RAM.
void crashlog_poll();
// A few words for Settings ("Crash in sync (LoadProhibited)", "Restarted: task watchdog",
// "Restarted: power dip (brownout)"), empty after a normal start or restart.
String crashlog_summary();
// Everything, for the serial port and the harness.
String crashlog_details();
