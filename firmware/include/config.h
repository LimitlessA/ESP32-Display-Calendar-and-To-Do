// Board wiring and build-time defaults. Wi-Fi, accounts and preferences are
// set on the device itself and stored in flash, so nothing here is secret.
#pragma once

// The GitHub build sets this to "<major>.<minor>.<build number>"; raise the
// major.minor here by hand for big changes.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.2.0"
#endif
// Which CrowPanel this build is for: platformio.ini sets one of these per build.
// The 7.0" is the default because it is the board in use.
#if !defined(BOARD_CROWPANEL_5) && !defined(BOARD_CROWPANEL_7)
#define BOARD_CROWPANEL_7
#endif
#ifdef BOARD_CROWPANEL_7
#define BOARD_NAME "CrowPanel 7.0\""
#define MANIFEST_FILE "manifest.json"
#else
#define BOARD_NAME "CrowPanel 5.0\""
#define MANIFEST_FILE "manifest-5inch.json"
#endif

// Every push to main publishes a GitHub release; this always points at the
// newest one, and each board size reads its own manifest.
#define DEFAULT_UPDATE_URL \
  "https://github.com/LimitlessA/ESP32-Display-Calendar-and-To-Do/releases/latest/download/" MANIFEST_FILE
#define HOSTNAME         "calendar"   // setup page at http://calendar.local

// TF card on SPI (same pins on the 5.0" and 7.0")
#define SD_SCK   12
#define SD_MISO  13
#define SD_MOSI  11
#define SD_CS    10

// Speaker amplifier on I2S (same pins on the 5.0" and 7.0")
#define I2S_BCLK 42
#define I2S_LRCK 18
#define I2S_DOUT 17

// Touch and the I2C header share this bus (LovyanGFX owns it as I2C port 1).
#define I2C_SDA 19
#define I2C_SCL 20
#define I2C_PORT 1

// The board has a battery charger but no way to measure the battery. A
// MAX17048 fuel gauge on the I2C header is detected automatically.
#define FUEL_GAUGE_ADDR 0x36

// Optional BH1750 light sensor on the I2C header (Settings > Features > Ambient light).
#define LIGHT_SENSOR_ADDR 0x23

// Optional PIR motion sensor: its output on the RX pin of the UART header.
// (GPIO38 on the GPIO_D header is not free: the touch controller needs it.)
#define MOTION_PIN 44

#define MAX_ACCOUNTS 6
