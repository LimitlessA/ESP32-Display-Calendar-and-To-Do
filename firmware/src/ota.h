#pragma once

#include <Arduino.h>

// Firmware updates without a cable, two ways:
//  - one tap on the device: reads the manifest at Settings::updateUrl,
//    {"version": "1.0.1", "url": "https://.../firmware.bin"}, and installs it;
//  - upload a firmware.bin on the setup page at http://calendar.local.
// Both write the inactive app slot and reboot into it when done.

enum class OtaState { Idle, Checking, UpToDate, Available, Installing, Done, Failed };

struct OtaStatus {
  OtaState state = OtaState::Idle;
  String latest;     // version offered by the manifest
  int progress = 0;  // percent while installing
  String message;    // error text when Failed
};

void ota_begin();
OtaStatus ota_status();
void ota_check();
void ota_install();

// Used by the setup page's upload handler so both paths report progress the same way.
bool ota_upload_start(size_t size);
bool ota_upload_write(const uint8_t *data, size_t len);
bool ota_upload_finish();
void ota_upload_abort(const String &why);
