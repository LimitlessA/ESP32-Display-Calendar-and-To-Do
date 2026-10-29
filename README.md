# ESP32 Display: Calendar and To-Do

A touch display that shows your upcoming meetings from several Google accounts
and your Google Tasks. From the screen you can dismiss a meeting, mark a task
done (with Undo), browse the week, get a chime before meetings, and pick one of
six themes. New firmware installs over Wi-Fi, so after the first flash you
never need a cable again.

| Home | Week | Tasks |
|---|---|---|
| Today, coming up and to-do side by side | Monday to Sunday at a glance | Filter by list, overdue or due today |

## Hardware

- **Elecrow CrowPanel ESP32 HMI 5.0"** (ESP32-S3-WROOM-1-N4R8, 800×480 RGB LCD,
  GT911 capacitive touch, 4 MB flash, 8 MB PSRAM).
- Optional: a microSD card (keeps an offline copy, so the display shows your
  data straight after boot or when the internet is down).
- Optional: a small 8 Ω speaker on the speaker connector for meeting chimes.
- Optional: a LiPo on the battery connector. The board can't measure the
  battery itself; add a MAX17048 fuel gauge on the I2C header (address 0x36) if
  you want the level shown in Settings.

## How it works

```
Google account 1 ─┐  Apps Script web app (one per account, protected by a token)
Google account 2 ─┼──────────── HTTPS ───────────► ESP32-S3 display
Google account N ─┘
```

Each Google account runs a small copy of `apps-script/Code.gs`. It reads that
account's calendars and Google Tasks and returns them as JSON. The display
fetches every account in the background (every 5 minutes by default, and right
away after you tap something), so nothing ever waits on the network while you
use the screen.

Outlook, iCloud or Exchange calendars: add their ICS link in Google Calendar
under *Other calendars › From URL*, and they appear on the display too.

## 1. Set up each Google account

Do this once per account you want on the display.

1. Sign in to the account and open <https://script.google.com/create>.
2. Replace the contents of `Code.gs` with `apps-script/Code.gs` from this repo.
3. Open *Project Settings*, tick *Show "appsscript.json"*, and replace that file
   with `apps-script/appsscript.json` (it turns on the Tasks service).
4. In `Code.gs`, change `TOKEN: 'change-me'` to a long random password.
5. Pick `preview` in the function list and press *Run*. Allow access when
   Google asks. The log shows what the display will receive.
6. *Deploy › New deployment › Web app*, with *Execute as: Me* and
   *Who has access: Anyone*. Copy the web app URL (it ends in `/exec`).

You add the URL and token to the display in step 3 below.

## 2. Flash the first firmware (once, over USB)

Every push to the repo builds the firmware on GitHub. Open the repo's
**Actions** tab, click the latest green *Build firmware* run, and download the
**firmware** artifact. Unzip it; you need `factory.bin`.

**Option A: from the browser (no install).**

1. Connect the board to your computer with a USB-C cable.
2. In Chrome or Edge, open <https://espressif.github.io/esptool-js/>.
3. Set the baud rate to 921600 and press *Connect*; choose the board's serial
   port (often "USB Serial" or "CH340").
   If it doesn't connect: hold **BOOT**, tap **RESET**, release **BOOT**, then
   try again.
4. Under *Flash Address* enter `0x0`, choose `factory.bin`, and press
   *Program*.
5. When it says it's done, press **RESET** on the board.

**Option B: with PlatformIO (if you want to change the code).**

```sh
pip install platformio
cd firmware
pio run -t upload        # builds and flashes over USB
pio device monitor       # optional: see the log
```

## 3. First start

1. The display opens the **Wi-Fi** screen. Tap your network, type the password
   on the on-screen keyboard, and tap the tick.
2. Go to **Settings › Manage accounts**. Scan the QR code with your phone (or
   open the address it shows, such as `http://192.168.1.42` or
   `http://calendar.local`, on any device on the same Wi-Fi).
3. On that page, add each account: a name, the web app URL from step 1 and its
   token. Changes need the 6-digit **PIN** shown on the display.

Meetings and tasks appear within a few seconds.

## Using it

- **Home**: today's meetings, the next days, and your to-do list. Tap ✕ to
  dismiss a meeting (it only hides it on the display, Google Calendar is not
  changed). Tap the circle to mark a task done; *Undo* appears for a few
  seconds. Tap a meeting for its details.
- **Week**: the whole week; use the arrows to move between weeks.
- **Tasks**: all tasks with filters for overdue, due today and each list.
  Marking a task done here completes it in Google Tasks.
- **Reminders**: a chime and a pop-up before each meeting, with *Snooze*.
- **Night mode**: between the hours you set, the display shows a dim clock
  and wakes on touch.
- **Settings**: brightness, chime volume, night mode, dim when idle, 12/24 h
  clock, theme, which calendars to show per account, and firmware updates.

### Themes

Settings › Display and sound › Theme: **Midnight**, **Ocean**, **Forest**,
**Sunset**, **Paper** (light) and **High contrast**. All text meets WCAG AA
contrast in every theme.

## Firmware updates over Wi-Fi (no PC)

**Automatic, on every push to main.** Each push to `main` builds the firmware
with a new version number (`1.0.<build number>`) and publishes it as the
latest GitHub release. The display looks for a new release every 3 hours and
installs it by itself when nobody is using it: during night mode, or after 15
minutes without a touch, and never in the 15 minutes before a meeting. The
update dialog shows the progress, the display restarts, and settings and
accounts are kept. Turn this off with Settings › Sync › *Auto-update*; you can
still install by hand with Settings › *Check for update*.

> The display downloads releases without signing in, so the repository must
> be **public** (it contains no passwords: tokens live only on the display).
> To change the update source, set another `manifest.json` address under
> *Update link* on the setup page; the format is
> `{"version":"1.1.0","url":"https://…/firmware.bin"}`.

**From the setup page.** Download the **firmware** artifact from the Actions
tab, open the display's setup page, choose `firmware.bin` (not `factory.bin`)
under *Firmware update*, and enter the PIN shown on the display.

For a big change, raise the major or minor number of `FIRMWARE_VERSION` in
`firmware/include/config.h`; the build number is added automatically.

## Why it feels fast

- LVGL 8 drawn by LovyanGFX straight into the RGB panel with DMA, from two
  strip buffers in internal RAM, at a 16 ms refresh period.
- Flat design: no shadows, blur or gradients, which are the slow parts of LVGL.
- The UI runs alone on core 1. Wi-Fi, HTTPS, the setup page and OTA run on
  core 0, so a slow network never blocks a tap.
- Taps act instantly on screen (optimistic update) and are sent to Google in
  the background, with an automatic roll-back if Google refuses.
- Every screen is built once at start-up; switching pages only shows it and
  refreshes what changed.
- Compiled with `-O2`.

## Project layout

```
apps-script/      Google Apps Script backend (one copy per account)
firmware/         PlatformIO project for the display
  src/            display, Wi-Fi, sync, SD cache, audio, OTA, setup page, UI
  src/fonts/      Montserrat with Romanian letters and icons (tools/gen_fonts.sh)
  sim/            desktop build of the UI that renders screenshots (sim/build.sh)
.github/workflows GitHub build: factory image, OTA image, releases
```

To check UI changes without the board, run `firmware/sim/build.sh` (needs gcc,
git and Python with Pillow); screenshots of every screen and theme land in
`firmware/sim/out/`.
