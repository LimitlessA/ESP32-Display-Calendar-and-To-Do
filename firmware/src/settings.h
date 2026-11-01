#pragma once

#include <Arduino.h>
#include <vector>

#include "config.h"

struct Account {
  String label;      // "Personal", "Work"
  String url;        // Apps Script web app URL ending in /exec
  String token;      // that script's CONFIG.TOKEN
  uint32_t color;    // badge color on the Accounts screen
};

// A published calendar link (Outlook "Publish calendar" ICS, iCloud public
// calendar, any .ics). The first Google account's script fetches and expands
// it, so its events arrive in the same feed as that account's.
struct IcsCalendar {
  String label;      // "Work (Outlook)"
  String url;        // https:// or webcal:// link to the .ics
  uint32_t color;
};
constexpr uint8_t kMaxIcs = 6;
// How often the display asks for changes (Settings > Sync), in minutes.
constexpr uint8_t kSyncChoices[] = {2, 3, 5, 10, 15, 30};

// Screens that come in three layouts (A, B, C), chosen like a theme.
enum class Layout : uint8_t {
  QuickAdd, Focus, Summary, Conflicts, LeaveBy, Dates, Review, Features,
  MeetTimer, Presence, Cost, Radar, Roulette, Desk, Count
};
constexpr uint8_t kLayoutCount = (uint8_t)Layout::Count;
// The ones picked from the mockups: 1B 2A 3A 4A 5B 7B 8C 9C (the photo frame, row 6, was dropped),
// then A for the meeting timer, phone presence, cost meter, radar, roulette and desk.
constexpr uint8_t kDefaultLayouts[kLayoutCount] = {1, 0, 0, 0, 1, 1, 2, 2, 0, 0, 0, 0, 0, 0};

struct Settings {
  String wifiSsid;
  String wifiPassword;
  std::vector<Account> accounts;
  std::vector<IcsCalendar> ics;

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
  uint8_t refreshMinutes = 3;   // polls are cheap: an unchanged feed is a few bytes
  uint8_t theme = 0;            // index into the theme table in theme.h
  String updateUrl = DEFAULT_UPDATE_URL;  // manifest for updates, empty = upload only
  bool autoUpdate = true;       // install new firmware by itself while nobody uses the display

  // Features, each with its own switch (Settings > Features)
  bool sound = true;            // chimes for reminders, leave-by alerts and the focus timer
  bool summary = true;          // "your day" screen every morning
  uint16_t summaryAt = 7 * 60;  // minutes after midnight
  bool review = true;           // plan unfinished tasks every evening
  uint16_t reviewAt = 20 * 60;
  bool conflicts = true;        // highlight overlapping meetings
  bool leaveBy = true;          // "leave now" before meetings with an address
  uint8_t leaveLead = 10;       // minutes of warning before you must leave
  bool focus = true;            // focus timer, started by holding a task
  uint8_t focusMinutes = 25;
  bool quickAdd = true;         // add tasks from the display
  bool countdowns = true;       // starred events count down
  bool dates = true;            // birthdays and public holidays
  bool ambient = false;         // follow the room light (needs a BH1750 sensor)
  bool motion = false;          // wake when someone walks up (needs a PIR sensor)
  bool offlineCopy = true;      // keep the last sync on the TF card
  bool meetTimer = true;        // time left in the meeting you are in
  bool presence = false;        // notice your phone over Bluetooth (pair it first)
  uint8_t awayMinutes = 10;     // phone not seen this long = away
  bool costMeter = false;       // what a meeting costs, people x hourly rate
  uint16_t costRate = 45;       // per person per hour
  bool radar = true;            // highlight tasks you keep postponing
  uint8_t radarMoves = 3;       // moves before a task counts as stuck
  bool roulette = true;         // let the display pick a task for you
  bool desk = false;            // control a smart standing desk over Wi-Fi
  String deskHost = "smartdesk-a43c.local";
  String deskUser;              // desk web UI account; empty = guest (memory buttons only)
  String deskPassword;
  uint8_t deskSitSlot = 1;      // memory button for sitting (M1-M4)
  uint8_t deskStandSlot = 2;    // ...and for standing
  bool deskMeetings = true;     // layout C: stand for meetings of 15 min or more
  bool deskFocus = false;       // layout C: stand while the focus timer runs
  bool deskAway = true;         // layout C: desk away mode while your phone is away
  uint8_t layouts[kLayoutCount] = {1, 0, 0, 0, 1, 1, 2, 2, 0, 0, 0, 0, 0, 0};  // see kDefaultLayouts

  uint8_t layout(Layout l) const { return layouts[(int)l]; }
};

// Loads from flash (or the SD card backup after a full erase). Call once.
void settings_begin();

// Thread-safe copy of the current settings.
Settings settings_get();

// Replaces the settings. They are saved to flash and backed up to SD by
// settings_flush() a few seconds later.
void settings_save(const Settings &s);

// Writes changed settings once they have been unchanged for a few seconds,
// or at once with now = true (before a restart). Call often, e.g. from loop().
void settings_flush(bool now = false);

// Bumps whenever settings change, so other modules can notice.
uint32_t settings_version();
