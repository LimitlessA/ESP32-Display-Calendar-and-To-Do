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

WifiState wifi_state();
String wifi_ssid();
int wifi_rssi();
String wifi_ip();

// Wi-Fi screen. Scanning runs in the background; poll wifi_scan_results().
void wifi_scan_start();
bool wifi_scan_results(std::vector<WifiNetwork> &out);  // false while scanning

// Try a network; it is saved only if the connection works.
void wifi_connect(const String &ssid, const String &password);
