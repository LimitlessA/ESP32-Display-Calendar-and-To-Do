#pragma once

#include <Arduino.h>

// Smart standing desk over Wi-Fi (Settings > Features > Smart desk), through
// the HTTP API of the desk's own web UI (SmartStandingDeskDualCore). Runs on
// its own task, so a slow or missing desk never holds up the screen.
// Without a desk login only the memory buttons and Stop work; raise, lower,
// the sit/stand timer and away mode need a desk account with the "user" role.

struct DeskStatus {
  bool online = false;     // the desk answered the last poll
  bool loggedIn = false;   // the desk accepted the login from the setup page
  bool hasLogin = false;   // a login is set on the setup page
  int heightMm = 0;
  bool moving = false;
  String state;            // SITTING, STANDING, MOVING, SLEEPING, EMERGENCY_STOP...
  int memoryMm[4] = {0, 0, 0, 0};  // M1-M4, 0 = unknown (needs the login)
  bool timerRunning = false;
  bool timerPaused = false;
  String timerPhase;       // SIT, STAND, IDLE
  long timerLeft = 0;      // seconds in this phase
  int sitMinutes = 0, standMinutes = 0;
  String error;            // last problem, for Settings
  uint32_t version = 0;    // bumps on every change
};

// Starts the background task. Does nothing until the feature is switched on.
void desk_begin();
DeskStatus desk_status();

// Commands. Each returns at once; the desk task sends it.
void desk_recall(uint8_t slot);  // memory position 1-4
void desk_hold(int direction);   // +1 raise, -1 lower while held, 0 released
void desk_stop();                // stops any movement (no login needed)
void desk_timer(bool on);        // the desk's own sit/stand interval timer
void desk_away(bool away);       // the desk's away mode
// The timer lengths (Settings::deskSitMin / deskStandMin) are sent with "start". After they changed,
// this restarts a running timer with them a moment later (a held + or - sends only the last value).
void desk_timer_apply();

// "Test connection" for the desk settings: runs on its own task, with the address and login as typed
// (not yet saved). Poll desk_test_result() for the outcome.
struct DeskTestResult {
  bool running = false;
  bool done = false;
  bool ok = false;
  String message;
};
void desk_test(const String &host, const String &user, const String &password);
DeskTestResult desk_test_result();
