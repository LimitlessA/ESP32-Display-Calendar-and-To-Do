#pragma once

#include <Arduino.h>
#include <vector>

struct Account {
  String label;      // "Personal", "Work"
  String url;        // Apps Script web app URL ending in /exec
  String token;      // that script's CONFIG.TOKEN
  uint32_t color;    // badge color on the Accounts screen
};

struct Settings {
  String wifiSsid;
  String wifiPassword;
  std::vector<Account> accounts;

  uint8_t brightness = 80;      // percent
  uint8_t volume = 60;          // percent
  bool chime = true;            // play a chime and show a reminder before meetings
  uint8_t remindMinutes = 5;
  bool nightMode = true;        // dim clock between nightStart and nightEnd
  uint16_t nightStart = 22 * 60;  // minutes after midnight
  uint16_t nightEnd = 7 * 60;
  bool dimIdle = false;         // dim the backlight after a while without touch
  uint8_t dimAfterMinutes = 2;
  bool use24h = true;
  uint8_t refreshMinutes = 5;
  uint8_t theme = 0;            // index into the theme table in theme.h
  String updateUrl;             // manifest for one-tap updates, empty = upload only
};

// Loads from flash (or the SD card backup after a full erase). Call once.
void settings_begin();

// Thread-safe copy of the current settings.
Settings settings_get();

// Replaces the settings, saves them to flash and backs them up to SD.
void settings_save(const Settings &s);

// Bumps whenever settings change, so other modules can notice.
uint32_t settings_version();
