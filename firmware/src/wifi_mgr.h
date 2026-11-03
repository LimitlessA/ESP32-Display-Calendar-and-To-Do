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

// Network watchdog. The sync and desk code report how each request went:
// ok = any reply from a server, failed = no connection or no DNS. Wi-Fi can
// stay "connected" while nothing gets through (seen 2026-11-03: DNS and every
// connection failing for minutes, the setup page down); then the connection is
// dropped and made again (the second time with the radio off and on too),
// every 3 minutes until a reply comes. The display never restarts for it.
void wifi_net_result(bool ok);

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
