#pragma once

#include <Arduino.h>

// Notices your phone over Bluetooth LE (Settings > Features > Phone presence).
// The display shows up on the phone as a Bluetooth accessory named
// "Calendar display". Once paired, the phone reconnects by itself whenever it
// is in range, and its private, changing address is recognised in scans too.
enum class PhoneState : uint8_t { Off, NotPaired, Near, Away };

// Starts Bluetooth when the feature is on; safe to call again after settings change.
void presence_apply(bool on);
PhoneState presence_state(uint8_t awayMinutes);
// Seconds since the phone was last seen, or -1 if never.
long presence_seen_ago();

// Pairing window: new phones can pair only while it is open.
void presence_pair(bool open);
bool presence_pairing();
int presence_paired_count();
void presence_forget();
