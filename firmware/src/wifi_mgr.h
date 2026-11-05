#pragma once

#include <Arduino.h>
#include <vector>

enum class WifiState { NotConfigured, Connecting, Connected, Failed, Disconnected };

struct WifiNetwork {
  String ssid;
  int rssi;
  bool locked;
};

void wifi_begin();

// Called by the sync task: keeps the saved network connected and handles
// requests from the Wi-Fi screen. Returns true while connected.
bool wifi_service();

// Network watchdog. The sync and desk code report how each request went
// (ok = any reply from a server). Wi-Fi can stay "connected" while nothing
// gets through (7", 2026-11-03 and -05), so wifi_service() also asks the
// router for a DNS name once a minute when no server answered lately. Three
// unanswered checks: the link is joined again (then with the radio off and on
// too). The display never restarts for it.
void wifi_net_result(bool ok);

// The last connection events (drops with the access point's reason, IP lost
// or got, watchdog actions), oldest first, one per line.
String wifi_log();

// True while Wi-Fi is down or stuck (joined, but the router does not answer).
// brief: for the header badge ("Wi-Fi down 14:02"); detail: why, since when,
// signal strength and what the display does about it.
bool wifi_problem(String &brief, String &detail);

// Low power: stops the radio before a light sleep (the connection drops) and
// starts it again after; the sync worker then reconnects at once.
void wifi_sleep(bool sleep);

WifiState wifi_state();
String wifi_ssid();
int wifi_rssi();
String wifi_ip();

// Wi-Fi screen. Scanning runs in the background; poll wifi_scan_results().
void wifi_scan_start();
bool wifi_scan_results(std::vector<WifiNetwork> &out);  // false while scanning

// Try a network; it is saved only if the connection works.
void wifi_connect(const String &ssid, const String &password);
