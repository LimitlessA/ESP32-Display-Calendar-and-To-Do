# Developer guide

For anyone who changes the code. Using the display is in
[user-guide.md](user-guide.md). First-time setup is in
[setup-guide.md](setup-guide.md).

Where the code and another document disagree, the code wins. This guide was
written from the code. Things that look unclear or wrong are marked **Not sure**
or **Possibly broken**.

## Contents

- [Repo layout](#repo-layout)
- [Architecture](#architecture)
- [The code convention](#the-code-convention)
- [Building with PlatformIO](#building-with-platformio)
- [CI and releases](#ci-and-releases)
- [Flash budget](#flash-budget)
- [Memory: internal RAM and PSRAM](#memory-internal-ram-and-psram)
- [Display stability](#display-stability)
- [The setup web page](#the-setup-web-page)
- [The desktop simulator](#the-desktop-simulator)
- [The test harness](#the-test-harness)
- [The test plan](#the-test-plan)
- [The Apps Script API](#the-apps-script-api)
- [The desk HTTP API](#the-desk-http-api)

## Repo layout

```
apps-script/
  Code.gs               Google Apps Script backend (one copy per Google account)
  appsscript.json       turns on the Tasks and Calendar advanced services
firmware/
  platformio.ini        two envs: crowpanel_7inch (default) and crowpanel_5inch
  min_spiffs.csv        partition table: two 1.9 MB app slots
  include/
    config.h            version, board name, pins, MAX_ACCOUNTS, default update URL
    lv_conf.h           LVGL 8.3 configuration
    lv_mem_psram.h      LVGL heap in PSRAM
  src/                  the firmware (below)
    fonts/              generated Montserrat and icon fonts (tools/gen_fonts.sh)
  web/index.html        source of the setup page
  sim/                  desktop build of the UI (sim/build.sh)
  tools/
    web_page.py         web/index.html -> src/web_page.h
    harness.py          PC side of the test harness
    gen_fonts.sh        regenerates src/fonts/*.c (needs Node.js)
    size_opt.py         build script: -Os for everything except LVGL
    bin_size.py         build script: fail if firmware.bin is over one slot
.github/workflows/build.yml   CI build, factory images, releases
docs/
  user-guide.md, setup-guide.md, developer-guide.md
  test-plan.md          end-to-end test plan
  architecture.drawio   C4 model and one flow per feature (generated)
  diagrams/build_drawio.py   generates architecture.drawio
```

Note that the tools live in `firmware/tools/`, not in a top-level `tools/`
folder. Run them from the `firmware` folder.

### Firmware modules (`firmware/src`)

| File | What it does |
|---|---|
| `main.cpp` | `setup()` starts every module. `loop()` copies new data to the UI, ticks the UI, flushes settings, polls the harness and runs LVGL. |
| `display.cpp/.h` | RGB panel (ESP-IDF driver), GT911 touch, backlight, LVGL flush. Pin maps and timing per board. |
| `settings.cpp/.h` | The `Settings` struct, JSON in NVS (`Preferences`, namespace `calendar`, key `cfg`) and an SD backup. Deferred writes. |
| `sync.cpp/.h` | The sync worker. Fetches feeds, queues actions, keeps the `Snapshot`, reads and writes the SD cache. |
| `model.h` | `Event`, `Task`, `DateItem`, `Snapshot` and friends. |
| `wifi_mgr.cpp/.h` | Connects, reconnects, scans. Called from the sync worker. |
| `web.cpp/.h`, `web_page.h` | The setup page server (port 80, mDNS `calendar.local`), the PIN, the upload route. `web_page.h` is generated. |
| `ota.cpp/.h`, `ota_root_cas.h` | Update check from a manifest, install, restart. |
| `desk.cpp/.h` | Smart desk client, on its own task. |
| `presence.cpp/.h` | Phone presence with NimBLE-Arduino. |
| `sensors.cpp/.h` | BH1750 light sensor (I2C) and PIR motion pin. |
| `battery.cpp/.h` | Optional MAX17048 fuel gauge. |
| `audio.cpp/.h` | Chimes on I2S. |
| `storage.cpp/.h` | SD card, atomic writes. |
| `harness.cpp/.h` | Serial and HTTP test commands, screenshots. |
| `theme.cpp/.h` | Six colour sets. |
| `icons.h` | Icon codes that match the generated icon font. |
| `google_root_cas.h`, `ota_root_cas.h` | Root certificates for HTTPS. |

### UI files (`firmware/src/ui_*.cpp`)

`ui_internal.h` is the shared header: sizes, the `Screen` enum, builders and
helpers. Each screen or overlay has its own file. Screens are built once and
refreshed when their data changes.

| File | Screen or overlay |
|---|---|
| `ui.cpp` | Navigation, clock labels, reminders, night mode, dimming, auto-update timing, toasts, rebuilds after a settings change |
| `ui_common.cpp` | Building blocks (box, label, button, chip, slider, stepper, switch), keyboard, nav header, time and date formatting |
| `ui_home.cpp`, `ui_detail.cpp`, `ui_week.cpp`, `ui_tasks.cpp`, `ui_night.cpp` | The main screens |
| `ui_wifi.cpp`, `ui_accounts.cpp`, `ui_settings.cpp`, `ui_features.cpp` | Settings screens |
| `ui_desk.cpp`, `ui_desk_settings.cpp` | The Desk screen, dock and desk login form |
| `ui_overlays.cpp` | Reminder, firmware update dialog, theme picker, calendar chooser |
| `ui_quickadd.cpp`, `ui_focus.cpp`, `ui_daily.cpp`, `ui_alerts.cpp`, `ui_dates.cpp`, `ui_meeting.cpp`, `ui_presence.cpp`, `ui_radar.cpp`, `ui_roulette.cpp` | One feature each, with its A, B and C layouts |

## Architecture

### Cores and tasks

- **Core 1** runs only the Arduino `loop()`: LVGL, touch, and `ui_tick()` every
  200 ms. Nothing on this core waits on the network.
- **Core 0** runs the tasks below, all created with `xTaskCreatePinnedToCore`:

| Task | File | Stack | Job |
|---|---|---|---|
| `sync` | `sync.cpp` | 16 KB | Wi-Fi service, NTP, feed fetch, queued actions, week fetch |
| `web` | `web.cpp` | 8 KB | `WebServer`, mDNS |
| `ota` | `ota.cpp` | 12 KB | Check, install, restart |
| `desk` | `desk.cpp` | 6 KB | Poll the desk, send commands |
| `audio` | `audio.cpp` | 4 KB | Play notes |
| `ble start` | `presence.cpp` | 6 KB | Starts Bluetooth once |
| `desktest` | `desk.cpp` | 8 KB, in PSRAM | The desk "Test connection" |

There is no separate Wi-Fi task. `wifi_service()` runs inside the sync worker.

### Data flow

```
Apps Script (per account) --HTTPS--> sync worker --> Snapshot (under a lock)
                                                          |
                                      sync_get_if_newer(version, copy)
                                                          v
                                      loop() -> ui_update(snapshot) -> screens
```

- The worker wakes every second when online (5 s offline). It fetches when the
  interval has passed (`Settings::refreshMinutes`), after an action, or after
  `sync_refresh()`.
- `Snapshot::version` bumps on every change. `main.cpp` copies the snapshot only
  when the version is newer.
- A touch action (`sync_dismiss_event`, `sync_complete_task`, ...) first changes
  the snapshot on screen, then adds an `Action` to a queue. The worker sends it.
  A failed action is retried 3 times, then rolled back with an error message.
- A `hidden` map keeps removed items out of the list until a fresh sync confirms
  them.
- Tasks added on the display get a temporary id `new:...`. `addedIds` maps it to
  Google's id, so the task can be completed before Google has answered.
- Two kept-open HTTPS connections (script.google.com and
  script.googleusercontent.com) are reused for 30 s. Apps Script answers with a
  redirect, which is followed by hand (max 4 hops).
- Feeds use format v2 with a `since` hash. An unchanged feed answers
  `{"same":true}` and the display redraws nothing.
- The first Google account's requests carry the ICS links (`&ics=...`).
- The time zone comes from `utcOffsetMin` in the feed (a fixed offset).
  **Not sure** how a daylight-saving change is handled between syncs; it is
  applied at the next feed.
- The SD card holds one cache file per account list
  (`/calendar/cache_<hash>.json`). It is read at start-up, to show something before
  the first sync. It is written only when the feed changed and Offline copy is on.

### Settings

`settings_get()` returns a copy under a lock. `settings_save()` replaces the
settings and bumps `settings_version()`. `ui_tick()` notices a changed version
(from the web page or Wi-Fi task) and rebuilds screens if a setting changes how
they look (`needsRebuild()` in `ui.cpp`). If you add a setting that changes the
layout of a screen, add it to `needsRebuild()`.

The JSON is written by `settings_flush()` 5 seconds after the last change, to
NVS and to `/calendar/settings.json` on the SD card. `settings_flush(true)` writes
at once, before a restart.

The backup on the SD card is plain text and includes the Wi-Fi password, the
tokens and the desk password.

### Layouts

`enum class Layout` lists the features that have three layouts (A, B, C).
`Settings::layouts[]` stores the choice, `kDefaultLayouts` the defaults. The
options window in `ui_features.cpp` builds the A/B/C picker from `kVariants`.

- `Layout::Features` still exists, but nothing lets you choose it, and all three
  names are "Tiles". The Features screen is always the tile grid.
- `Layout::Dates` is shared by two cards (Countdowns and Birthdays and holidays).

### Features screen

`kFeatures[]` in `ui_features.cpp` has one row per card: name, subtitle, which
`bool` in `Settings` it switches, its numeric option, and its layout. To add a
feature: add the setting, add a row, and add the screens.

## The code convention

Each source file starts with a block called **Texts and settings**, then
**State**, then **Code**:

```cpp
// ---- Texts and settings ------------------------------------------------------
constexpr const char *kToday = "Today";   // every on-screen text and tunable number
// ---- State -------------------------------------------------------------------
lv_obj_t *body;                           // file-level variables
// ---- Code --------------------------------------------------------------------
```

- Put every user-visible string and every timing or size in the first block, so
  wording and tuning can change without reading the code.
- Texts use `printf`-style templates and `strf()`.
- File-local things go in an anonymous namespace.

**Not sure** the convention is followed everywhere. These files have none of the
three markers: `audio.cpp`, `battery.cpp`, `harness.cpp`, `main.cpp`,
`settings.cpp`, `storage.cpp`, `theme.cpp`, `wifi_mgr.cpp`. `display.cpp` has
"Texts and settings" and "Code" but no "State". `ui_common.cpp` has only
"Texts and settings". `ui_night.cpp` has no "Code" marker.

## Building with PlatformIO

Two environments, both in `firmware/platformio.ini`:

| Env | Board | Defines |
|---|---|---|
| `crowpanel_7inch` (default) | Elecrow CrowPanel 7.0" | `BOARD_CROWPANEL_7` |
| `crowpanel_5inch` | Elecrow CrowPanel 5.0" | `BOARD_CROWPANEL_5` |

The platform is **pioarduino** `55.03.311` (Arduino 3.3 on ESP-IDF 5.5), pinned by
URL. Libraries are pinned too: LovyanGFX 1.2.32, LVGL 8.3.11, ArduinoJson 7.2.1,
NimBLE-Arduino 1.4.3.

```powershell
pip install platformio==6.1.19
cd firmware
pio run -e crowpanel_7inch            # or crowpanel_5inch
pio run -e crowpanel_7inch -t upload  # flash over USB
pio device monitor                    # serial log, 115200
```

Rules that matter:

- **Pin PlatformIO to 6.1.19.** pioarduino 55.03.x needs Core 6.1.19. Version 6.2's
  SCons breaks it. CI uses `pip install platformio==6.1.19 esptool==4.8.1`.
- **No spaces in the path on Windows.** The ESP-IDF build breaks on them.
- **Build from PowerShell** (not Git Bash or an IDE terminal that changes the
  environment).
- **The first build after a `custom_sdkconfig` change may fail at the link step.**
  pioarduino rebuilds the ESP-IDF libraries with your options, which is slow.
  **Run the same command again.** CI does this on purpose (`pio run ... || pio
  run ...`).
- Generated files (`managed_components/`, `dependencies.lock`, `CMakeLists.txt`,
  `sdkconfig.*`, `.dummy/`, `idf_component.yml`) are in `.gitignore`.

The version shown in Settings comes from `FIRMWARE_VERSION`. CI overrides it with
`PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_VERSION=\"0.2.N\""`. For a local build it is
`0.2.0` from `include/config.h`. Raise major.minor there by hand for a big
change. The build number is added by CI.

Other build facts:
- `-O2` is the global flag. `tools/size_opt.py` turns it into `-Os` for everything
  except LVGL.
- `tools/bin_size.py` runs after the build and fails it if `firmware.bin` is more
  than 1,966,080 bytes. It prints `firmware.bin: N of 1966080 bytes (X free)`.
- C++17 (`-std=gnu++17`) to match the simulator.
- `-DCORE_DEBUG_LEVEL=1`.

## CI and releases

`.github/workflows/build.yml` runs on every push and pull request.

1. Installs Python 3.12, PlatformIO 6.1.19 and esptool 4.8.1. Caches `~/.platformio`
   and `firmware/.pio`.
2. **Picks the version**: `<major>.<minor from config.h>.<github run number>`. With
   `config.h` at `0.2.0` this gives **`0.2.<run>`**.
3. Builds both envs (`SCONSFLAGS=-k` to list every error). Each build is run twice
   if the first fails. Then it checks that the version string is inside each
   `firmware.bin`, and prints the size against 1,966,080 bytes.
4. **Makes the factory images** with `esptool.py merge_bin`, 4 MB, at these offsets:

   | Offset | File |
   |---|---|
   | `0x0` | `bootloader.bin` |
   | `0x8000` | `partitions.bin` |
   | `0xe000` | `boot_app0.bin` |
   | `0x10000` | `firmware.bin` |

5. Writes the files to `dist/`:

   | 7.0 inch | 5.0 inch |
   |---|---|
   | `factory.bin` | `factory-5inch.bin` |
   | `firmware.bin` | `firmware-5inch.bin` |
   | `manifest.json` | `manifest-5inch.json` |

   A manifest is `{"version":"0.2.N","url":"https://github.com/<repo>/releases/download/v0.2.N/firmware[-5inch].bin"}`.
6. Uploads `dist/` as the **`firmware`** artifact.
7. **On a push to any branch other than `main`:** force-pushes `dist/` as the root
   of a branch named **`builds/<branch name>`**. This lets you fetch a branch's
   firmware with plain git, before it is merged. Each push overwrites it. A
   branch named `claude/idf5-migration` becomes `builds/claude/idf5-migration`.
8. **On a push to `main`:** publishes a GitHub release tagged `v<version>`, named
   "Firmware <version> (not yet tested on a real board)", marked latest. Its files
   are all of `dist/`.

The display reads
`https://github.com/LimitlessA/ESP32-Display-Calendar-and-To-Do/releases/latest/download/manifest.json`
(or `manifest-5inch.json`). That address is `DEFAULT_UPDATE_URL` in `config.h`.
If you fork the repository, change it. The repository must be public.

Pull request builds produce only the artifact.

## Flash budget

The partition table (`min_spiffs.csv`) on 4 MB flash:

| Name | Offset | Size |
|---|---|---|
| `nvs` | `0x9000` | `0x5000` |
| `otadata` | `0xe000` | `0x2000` |
| `app0` (ota_0) | `0x10000` | `0x1E0000` = **1,966,080 bytes** |
| `app1` (ota_1) | `0x1F0000` | `0x1E0000` |
| `spiffs` | `0x3D0000` | `0x20000` |
| `coredump` | `0x3F0000` | `0x10000` |

- The firmware must fit one OTA slot: **1,966,080 bytes**.
  `board_upload.maximum_size` is set to that, and `tools/bin_size.py` enforces it.
  PlatformIO's own size check adds up ELF sections and misses the padding that
  ESP-IDF adds when the program runs from PSRAM. A too-big image would leave the
  display unable to boot.
- CI prints the free space for each env as a notice.
- Ways the build saves flash (see `platformio.ini`): `-Os` for everything but
  LVGL, `custom_component_remove` for libraries the Arduino core bundles but this
  firmware does not use (RainMaker, Zigbee, esp-sr, ...), Zigbee and Insights off,
  mbedTLS trimmed (client only, common certificate bundle, fewer curves), IPv6
  off, core dump off.
- **Not sure** the `spiffs` partition is used. Nothing in `src/` mounts SPIFFS or
  LittleFS. It looks like left-over space.
- The `coredump` partition exists, but core dump is turned off in `custom_sdkconfig`.

## Memory: internal RAM and PSRAM

The board has 8 MB of PSRAM and about 300 KB of usable internal RAM. Internal RAM
is needed by Wi-Fi, TLS, DMA and Bluetooth. Most other things live in PSRAM.

What lives where, and why:

| What | Where | Why |
|---|---|---|
| The frame buffer (800 x 480 RGB565) | PSRAM | Too big for internal RAM. The RGB panel driver owns it. |
| LVGL draw buffer: 12 lines (about 19 KB) | Internal RAM (PSRAM if that fails) | Fast. 12 lines, not 24, to leave room for Bluetooth. A second buffer would never be used, because `flush_cb` copies into the frame before it returns. |
| LCD bounce buffers: 2 x 10 lines (2 x 16 KB) | Internal RAM | Espressif's fix for "drift": the LCD reads from internal RAM that is refilled from PSRAM. |
| The LVGL heap (all UI objects) | PSRAM (`lv_mem_psram.h`) | Thousands of small objects used up internal RAM, so Wi-Fi could not scan and the board rebooted in a loop. |
| Any `malloc` of 512 bytes or more | PSRAM (`heap_caps_malloc_extmem_enable(512)` in `setup()`) | Calendar and task data (many strings, JSON) filled internal RAM until Wi-Fi and HTTPS ran short. The 7 inch went down to 1.4 KB free. |
| Wi-Fi and lwIP buffers | PSRAM (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`) | Internal RAM. |
| mbedTLS buffers | PSRAM (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`) | Internal RAM. |
| NimBLE buffers | PSRAM (`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`) | Leaves internal RAM to the radio. |
| The desk "Test connection" task stack (8 KB) | PSRAM (a static task) | With Bluetooth on, the task could not be created in internal RAM. |
| The harness log ring (8 KB) and screenshot buffer | PSRAM | Debug only. |
| The program itself | PSRAM (XIP) | See below. |

A comment in `platformio.ini` says NimBLE-Arduino reads its options from build
flags (`nimconfig.h`), but `build_flags` contains no NimBLE flags. Only the
`custom_sdkconfig` NimBLE options are set. **Not sure** which of the two
NimBLE-Arduino 1.4.3 really uses here.

Bluetooth needs special care:
- The Arduino core frees the Bluetooth controller's memory (about 36 KB of
  internal RAM) at start-up unless `bleInUse()` says it is needed. `presence.cpp`
  defines `bleInUse()`: it reads the saved settings from NVS and keeps the memory
  only if `"presence":true`.
- **Result:** turning Phone presence on for the first time restarts the display
  once, so the memory is kept at the next boot.
- Bluetooth starts only if at least 28 KB of internal RAM is free, with a 16 KB
  largest block. Otherwise it shows an error instead of crashing.

Watch `heap` from the harness: the test plan wants internal free above 30 KB, and
the minimum above 20 KB during a 30-minute idle test.

## Display stability

A picture that "drifts" or flickers is the main risk on this hardware. Flash
writes and heavy PSRAM use can starve the LCD. What the code does about it:

- **XIP from PSRAM** (`CONFIG_SPIRAM_XIP_FROM_PSRAM=y`). The program runs from
  PSRAM, so writing to flash (an update, saving settings) no longer pauses the
  cache that feeds the screen. This needs ESP-IDF 5, which is why the project
  moved to pioarduino. Arduino 2 (ESP-IDF 4.4) cannot do it.
- **Bigger cache:** 64 KB data cache, 64-byte lines.
- **Bounce buffers.** The RGB driver reads two 10-line buffers in internal RAM,
  refilled from the frame buffer in PSRAM.
- **`CONFIG_LCD_RGB_ISR_IRAM_SAFE=y`** and **`CONFIG_LCD_RGB_RESTART_IN_VSYNC=y`**.
  The LCD interrupt keeps working during flash operations, and a restart happens
  at the vertical sync.
- **Pixel clock:** 24 MHz on the 7 inch (about 49 fps; 15 MHz flickered), 15 MHz on
  the 5 inch (about 35 fps; 12 MHz flickered).
- **Deferred settings writes.** Writing to flash pauses the PSRAM cache, so the
  picture could jump. `settings_flush()` waits until settings have been left
  alone for 5 seconds. Flipping several toggles costs one write. The UI also
  batches option changes (about 0.6 s) before saving.
- **Dark during an update.** `ota.cpp` calls `display_blank(true)` while
  installing, then restarts.
- **Screenshots** read the frame buffer in slices of 8 rows with a 1 ms pause.
  Reading it all at once starved the LCD and glitched the picture.
- **Touch** is sampled every 10 ms from inside `flush_cb`, and a press is held
  until LVGL has read it. Without this a quick tap that starts and ends during a
  long redraw was never seen.
- **Draw order:** `flush_cb` copies into the frame and writes the cache back before
  returning.
- The 7 inch V3.0 board resets its touch controller through a PCA9557 I/O expander
  at start-up, and keeps IO2 to IO7 high because the expander also drives the
  LCD's power and reset lines. A 5 inch image on a 7 inch board (or the other way
  round) shows lines and flicker.

## The setup web page

- Source: `firmware/web/index.html`. One file, with inline CSS and JavaScript.
- `firmware/tools/web_page.py` removes comments and indentation, gzips the HTML
  and writes it as a byte array to `firmware/src/web_page.h`.
- `web_page.h` is **generated and checked in**. After you edit `index.html`, run
  from the `firmware` folder:

  ```
  python3 tools/web_page.py
  ```

  and commit both files. The build does not run the generator.
- `web.cpp` serves the gzipped bytes at `/` with `Content-Encoding: gzip`. The
  page never changes at run time; everything it shows comes from `GET /state`.

Routes in `web.cpp` (the PIN is a form field named `pin`):

| Route | Method | PIN | What |
|---|---|---|---|
| `/` | GET | no | The page |
| `/state` | GET | no | JSON status: version, Wi-Fi, accounts, ICS, desk, sync interval, update link, OTA state. No tokens or passwords. |
| `/pin` | POST | yes | Check the PIN |
| `/account/add`, `/account/remove` | POST | yes | Google accounts. The URL must start with `https://script.google.com/` and end with `/exec`. |
| `/ics/add`, `/ics/remove` | POST | yes | Calendar links |
| `/settings/sync` | POST | yes | Sync interval (1 to 60; the page offers 2 to 30) |
| `/settings/update-url` | POST | yes | Manifest address |
| `/settings/desk` | POST | yes | Desk address, user, password |
| `/settings/desk/test` | POST, GET | yes | Start a desk test, then poll for the result |
| `/update` | POST | yes (form field) | Upload `firmware.bin` |
| `/harness` | GET | yes | One harness command |
| `/shot.bmp` | GET | yes | Screenshot |

The PIN is 6 digits, made from `esp_random()` at every boot. Five wrong PINs lock
changes for 60 seconds (HTTP 429).

## The desktop simulator

`firmware/sim/build.sh` builds the UI for a PC and renders every screen to PNG, so
layouts can be checked without the board.

```
cd firmware
sim/build.sh [output dir]      # default output: sim/out
```

- Needs `gcc`, `g++`, `git`, `python3` with Pillow.
- It clones LVGL v8.3.11 into `firmware/.sim/lvgl` on the first run.
- It compiles `src/theme.cpp`, `src/ui*.cpp`, `sim/sim_stubs.cpp` and
  `sim/sim_main.cpp`, with C++17 and `-Isim`, `-Iinclude`, `-Isrc`.
- `sim_stubs.cpp` stands in for the network, settings, desk, presence and so on.
  `sim_main.cpp` sets up sample data and renders each screen and theme.
- `.sim/` and `sim/out/` are in `.gitignore`.
- If you add a new `ui_*.cpp` file, it is picked up by the glob. If it needs a
  new function from a non-UI module, add a stub.

## The test harness

The harness lets a PC drive the display. It is built in unless `HARNESS` is
defined as 0 (`-DHARNESS=0`).

**Serial** (115200, one command per line, reply starts after `H> `):

| Command | What |
|---|---|
| `tap X Y` | Touch at a point |
| `press X Y MS` | Hold for MS milliseconds |
| `swipe X1 Y1 X2 Y2 [MS]` | Drag |
| `screen NAME` | Show a screen: `home detail week tasks night wifi accounts settings features dates desk` |
| `shot` | Screenshot (run-length pairs, base64) |
| `heap` | Internal and PSRAM free, largest block, minimum |
| `log` | The last 8 KB of log lines |
| `pin` | The setup page PIN and address (serial only) |
| `help` | List |

**HTTP:** the same commands at `GET /harness?pin=<PIN>&cmd=<command>`, and the
screen at `GET /shot.bmp?pin=<PIN>`. `shot` and `pin` are not available through
`/harness`.

The commands run on the UI task, through a queue, so they never race with LVGL.

**PC side:** `firmware/tools/harness.py` (needs `pyserial` and `Pillow`):

```
python tools/harness.py --port COM7 cmd "tap 400 240"
python tools/harness.py --port COM7 shot home.png
python tools/harness.py --http 192.168.1.50 --pin 123456 shot home.png
python tools/harness.py --http 192.168.1.50 --pin 123456 cmd "screen week"
python tools/harness.py decode --env crowpanel_7inch "Backtrace: 0x4200..:0x3fc9.. ..."
```

- Opening the serial port keeps the reset lines released, so the display does
  not restart.
- `decode` finds the code addresses in a backtrace and resolves them with
  `xtensa-esp32s3-elf-addr2line` against `.pio/build/<env>/firmware.elf`.

**Possibly a concern:** the HTTP harness is on in release builds and is reachable
by anyone who knows the PIN. It can tap the screen and take screenshots, which can
include account names and task titles. Consider building releases with
`-DHARNESS=0`.

## The test plan

`docs/test-plan.md` is the end-to-end plan, run on both boards after each merge.
It is driven by the harness, so each step leaves a screenshot. Sections:

1. Regression list (R1 to R16, Andrei's items of 1 Nov).
2. Every screen.
3. Every feature on and off.
4. Themes and layouts.
5. Sync and accounts.
6. Updates (OTA).
7. Stability.

Conventions: a step passes when the screenshot shows the expected screen, the
board did not reset, and internal free memory stays above 30 KB. The plan names
COM7 for the 7 inch and COM16 for the 5 inch.

Things in the plan that do not match the code:
- **R3** asks for a "weather icon" on Home. There is no weather feature in the
  firmware, the Apps Script or the setup page.
- **6.1** expects the setup page to refuse or flag the other board's
  `firmware.bin`. The upload code does not check the board (see below).

## The Apps Script API

`apps-script/Code.gs` is a web app. Every call is `GET <web app URL>?token=T&...`.
Times are epoch milliseconds. Replies are JSON. A wrong token gives
`{"error":"unauthorized"}`. Any error gives `{"error":"message"}`.

| `action` | Parameters | What |
|---|---|---|
| (none) | `v=2`, `from=MS`, `since=HASH`, `ics=URL` (repeatable) | The feed |
| `range` | `from`, `to` (max 45 days), `v=2`, `ics=URL` | Events in a range (the week view) |
| `complete` | `list`, `task` | Mark a task done (a task deleted elsewhere counts as done) |
| `uncomplete` | `list`, `task` | Undo it (the Done tab's Restore). The due date is kept. |
| `dismiss` | `event`, `until` | Hide one event occurrence on the display |
| `showcal` | `id`, `shown=0\|1` | Show or hide one calendar |
| `add` | `title`, `list`, `due` (yyyy-MM-dd) | Add a task. No list = the first list. |
| `due` | `list`, `task`, `due` | Move a task. Empty = no date. |
| `star` | `event`, `on=1\|0`, `title`, `start`, `allDay` | Star an event for a countdown |

Dismissed events, hidden calendars and starred events are stored in the script's
properties. `clearDismissed()` (run from the editor) brings back dismissed
events. `preview()` logs the feed and grants permissions.

Limits in `CONFIG`: 150 events per feed (`MAX_FEED_EVENTS`), 150 per range, 50 open
tasks (`MAX_TASKS`), 20 done tasks from the last 7 days, 6 ICS links per request,
300 events per ICS link and window.

### Feed format v2

The display sends `v=2`. The reply (a format 1 reply, without `v`, is kept for
older firmware):

```jsonc
{
  "account": "me@gmail.com",
  "tz": "Europe/Bucharest",
  "utcOffsetMin": 120,
  "calendars": [
    {"id": "...", "name": "Work", "color": "#7986cb", "shown": true},
    {"id": "ics:1", "name": "Outlook", "ics": 1, "n": 12, "err": "optional"}
  ],
  "events": [
    {"k": "uid|startMs", "s": 1700000000000, "e": 1700003600000, "t": "Title", "c": 0,
     "a": true, "l": "place", "d": "notes", "o": "organizer", "g": 3, "tr": 25}
  ],
  "tasks": [{"id": "", "listId": "", "title": "", "list": "", "dueKey": "2026-11-02", "notes": "", "parent": ""}],
  "done":  [{"id": "", "listId": "", "title": "", "list": "", "dueKey": "", "at": 1700000000}],
  "lists": [{"id": "", "title": ""}],
  "dates": [{"kind": "birthday|holiday|star", "title": "", "day": "2026-11-05", "key": ""}],
  "frequent": ["Send invoice"],
  "from": 0, "to": 0,
  "hash": "22 characters"
}
```

- Events use short keys and leave out empty fields. `a`, `l`, `d`, `o`, `g` and
  `tr` appear only when set. `c` is the index into `calendars`. An ICS calendar
  is an entry with `ics` set. Its colour and name come from the display.
- The feed starts at this week's Monday (the display sends its own as `from`, if
  it is within the last 8 days) and covers 14 days, at least 7 days from now.
  `from` and `to` say what the feed covers, so the week view needs no request
  for those weeks. If the event limit cuts the list, `to` is the start of the
  first event left out.
- `hash` is a fingerprint of the whole reply. If the request has `since=<hash>`
  and nothing changed, the reply is `{"same":true,"hash":"..."}`.
- `tr` is travel minutes from `HOME_ADDRESS`, for meetings with a place that
  start within `TRAVEL_HOURS_AHEAD`.
- Event `key` is `iCalUID|startMs`, because recurring events share one UID.
- Declined events, cancelled events and working-location entries are left out.
- Birthdays come from the primary calendar's birthday events, or the Contacts
  calendar. Holidays come from `HOLIDAYS_CALENDAR`. Both are cached for 6 hours.
- The token is in the query string. Anyone with the web app URL and the token can
  read and change that account's calendar and tasks.

## The desk HTTP API

The desk runs the MicroPython firmware of SmartStandingDeskDualCore
(`web_server.py`, `api_handlers.py`). `desk.cpp` uses these routes. The base URL is
`http://<deskHost>`; a `.local` name is resolved with mDNS and the IP is cached.
Calls time out after 1.5 s.

| Route | Method | Login | Sent by the display |
|---|---|---|---|
| `/api/desk/status` | GET | no | Every 2 s (0.5 s while moving): `height_mm`, `is_moving`, `state` |
| `/interval/status` | GET | no | With each poll: `running`, `paused`, `phase`, `remaining_seconds`, `sit_minutes`, `stand_minutes` |
| `/api/auth/login` | POST | no | `{"username","password"}`, returns `token`. Used as `Authorization: Bearer <token>`. Re-done on a 401. |
| `/api/desk/memory` | GET | yes | Every 5 minutes: memory heights (accepts several JSON shapes) |
| `/position` | POST | no | `{"position":N}` for N from 1 to 4. Falls back to `{"position":"MN"}`. Retried signed in on 401 or 403. |
| `/api/desk/emergency-stop` | POST | no | Stop |
| `/api/desk/hold` | POST | user | `{"direction":"up"\|"down"}` every 350 ms while held |
| `/api/desk/hold/stop` | POST | user | On release |
| `/interval/start` | POST | user | `sit_min`, `stand_min`, `sit_target_mm`, `stand_target_mm` |
| `/interval/stop`, `/interval/pause`, `/interval/resume` | POST | user | The ring buttons |
| `/api/away/start`, `/api/away/end` | POST | yes | Away mode |

Notes from reading the desk code:
- The display logs every memory call to the serial port (`[desk] ...`).
- The desk checks the **user** role for hold, hold/stop and interval routes. Away
  start and end only check that you are logged in.
- `/interval/start` returns 403 "Outside working hours" if the desk user has working
  hours set and it is outside them. The display ignores the reply code, so
  nothing is shown. The desk also stops a running timer by itself outside
  working hours.
- **Possibly broken:** the desk's timer phase is `IDLE`, `SITTING`, `STANDING` or
  `PAUSED`. `ui_desk.cpp` compares the phase with `"SIT"` (and `desk.h` says
  "SIT, STAND, IDLE"). The comparison never matches, so the display always takes
  the sit phase for the stand phase.
- **Possibly a concern:** the display's comment says the desk stops a hold about
  500 ms after the last beat. The desk's `hold_timeout_ms` default is 10000, so if
  the display stops sending (for example it crashes) while a button is held, the
  desk may keep moving for up to 10 seconds.

## Things that look unclear or possibly broken

Found while writing these documents. None were changed in the code.

- The desk timer phase mismatch above.
- The desk hold timeout above.
- **No board check on firmware upload.** Neither `/update` nor `ota.cpp` checks
  that the image is for the 7 inch or the 5 inch. The test plan expects it to be
  refused or flagged. The auto-update path is safe, since each board reads its
  own manifest.
- `README.md` had stale text (version `0.1.<n>`, "LVGL and LovyanGFX compiled with
  -O2", "Settings > Manage accounts", a Features-screen layout list). It is now
  short and points here.
- Night mode hours, the reminder lead time (5 min) and the dim delay (2 min)
  have settings fields but no control on the display or the setup page.
- `ui_accounts.cpp` has a weak `desk_settings_show()` that says the real one
  arrives "on the desk branch". The real one is in `ui_desk_settings.cpp`, so the
  weak one is dead code.
- The `spiffs` partition seems unused.
- `Layout::Features` is stored but unused.
- Per-meeting reminder overrides and snoozes are kept in RAM only.
- The radar counts are saved only to the SD card.
- Meeting cost counts `guests` (attendees who are not the organiser), at least 1.
- `settings.json` on the SD card holds secrets in plain text.
- The sync interval route accepts 1 to 60 minutes, but the display and page offer
  2 to 30.
