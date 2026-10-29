// Board wiring and build-time defaults. Wi-Fi, accounts and preferences are
// set on the device itself and stored in flash, so nothing here is secret.
#pragma once

#define FIRMWARE_VERSION "1.0.0"
#define HOSTNAME         "calendar"   // setup page at http://calendar.local

// CrowPanel 5.0": TF card on SPI
#define SD_SCK   12
#define SD_MISO  13
#define SD_MOSI  11
#define SD_CS    10

// CrowPanel 5.0": speaker amplifier on I2S
#define I2S_BCLK 42
#define I2S_LRCK 18
#define I2S_DOUT 17

// Touch and the I2C header share this bus (LovyanGFX owns it as I2C port 1).
#define I2C_PORT 1

// The board has a battery charger but no way to measure the battery. A
// MAX17048 fuel gauge on the I2C header is detected automatically.
#define FUEL_GAUGE_ADDR 0x36

#define MAX_ACCOUNTS 6
