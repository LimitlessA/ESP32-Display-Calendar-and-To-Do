#pragma once

#include <Arduino.h>

// Setup page at http://calendar.local (or the device's IP): add and remove
// Google accounts, set the update link, and upload new firmware. Changes need
// the 6-digit PIN shown on the device's Accounts screen.
void web_begin();
String web_pin();
String web_url();
