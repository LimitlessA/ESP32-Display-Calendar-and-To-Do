# ESP32 Display: Calendar and To-Do

A touch display that shows your upcoming meetings from several Google accounts
and your Google Tasks. From the screen you can dismiss a meeting, mark a task
done (with Undo), add tasks, run a focus timer, browse the week, and get a
chime before meetings, a "leave now" alert, a warning when meetings overlap, a
morning summary and an evening review. Pick one of six themes, and for each
feature one of three layouts. New firmware installs over Wi-Fi, so after the first flash you
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
- Optional: a **BH1750** light sensor (GY-302 board) on the I2C header (3V3,
  GND, SDA, SCL; address 0x23). With *Ambient light* on, the screen dims in a
  dark room.
- Optional: a 3.3 V **PIR motion sensor** (for example AM312) with its output
  on **IO44**, the RX pin of the UART header. With *Motion wake* on, the screen
  wakes when someone walks up. IO44 is shared with the USB serial chip, so
  unplug the sensor while flashing over USB.

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
   with `apps-script/appsscript.json` (it turns on the Tasks and Calendar
   services).
4. In `Code.gs`, change `TOKEN: 'change-me'` to a long random password.
   For leave-by alerts, set `HOME_ADDRESS` to where you leave from (and
   `TRAVEL_MODE` to `driving`, `walking`, `bicycling` or `transit`). For public
   holidays of another country, change `HOLIDAYS_CALENDAR`.
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

### Features (Settings › Features)

Each one has its own switch; some have an option such as a time.

| Feature | What it does |
|---|---|
| Quick add task | *+ Add a task* on Home and Tasks; suggests tasks you complete often |
| Focus timer | Hold a task to start a timer (25 min by default); ends with *Mark task done* |
| Daily summary | Your day at a glance every morning (07:00 by default) |
| End-of-day review | Every evening, move or finish what is still open |
| Conflict alerts | Highlights meetings that overlap, across all accounts |
| Leave-by alerts | Warns before you must leave, using Google Maps travel time from `HOME_ADDRESS` |
| Countdowns | Star a meeting in its details to count down to it |
| Birthdays and holidays | From Google Contacts and the public holiday calendar |
| Reminder sound | Chimes for reminders, leave-by alerts and the focus timer |
| Ambient light | Follows the room light (needs the BH1750) |
| Motion wake | Wakes the screen when someone walks up (needs the PIR) |
| Offline copy | Keeps the last sync on the microSD card |
| Meeting timer | Time left in the meeting you are in, and a soft chime 5 minutes before the end |
| Meeting cost | What a meeting costs: people × hourly rate (€45 by default) × time |
| Phone presence | Notices your phone over Bluetooth; pair it once from the feature's options |
| Procrastination radar | Counts how often each task is pushed back; "stuck" after 3 moves by default |
| Task roulette | *Pick for me* on Tasks lets the display choose, favouring overdue tasks |
| Smart desk | Controls a standing desk running [SmartStandingDeskDualCore](https://github.com/LimitlessA/SmartStandingDeskDualCore) |

### Layouts

Settings › Features › *Layouts* works like the theme picker. Fourteen features
come in three layouts each, and you can mix them; the presets are *Your picks*,
*All A*, *All B* and *All C*.

| Feature | A | B | C |
|---|---|---|---|
| Quick add task | Keyboard | Frequent tiles (pick) | Sheet over the list |
| Focus timer | Big ring (pick) | Split screen | Header strip |
| Daily summary | Stat tiles (pick) | Timeline | Briefing |
| Conflict alerts | Banner on Home (pick) | Compare popup | Marked in Week |
| Leave-by alert | Full screen | Card on Home (pick) | Header countdown |
| Countdowns and dates | Home column | Ribbon (pick) | Dates tab |
| End-of-day review | Checklist | One at a time | Plan by day (pick) |
| Features screen | List | Tabs | Tiles (pick) |
| Meeting timer | Progress on the meeting card (pick) | Full-screen countdown | Glowing top edge |
| Phone presence | Welcome back summary (pick) | Privacy mode: titles read "Busy" | Away screen |
| Meeting cost | On the meeting card (pick) | Big counter | Weekly receipt (Week screen) |
| Procrastination radar | Glowing tasks (pick) | Daily nudge popup | Radar view (Tasks screen) |
| Task roulette | Wheel (pick) | Card deck | Slot machine |
| Smart desk | Dock on Home (pick) | Desk screen | Desk follows your calendar |

### Phone presence

Turn on Settings › Features › *Phone presence*, open it and tap *Pair a
phone*. On the phone, open Bluetooth settings and pick **Calendar display**.
It shows up as a media remote with no keys, so it never acts as a keyboard.
From then on the phone reconnects by itself, and the display also recognises
the phone's private Bluetooth address in passing. You count as away after 10
minutes without your phone (an option). iPhones keep the link up reliably;
some Android phones drop it to save battery, so allow a longer away time
there. *Forget* removes the pairing on the display; also remove it on the
phone.

### Smart desk

For a desk running
[SmartStandingDeskDualCore](https://github.com/LimitlessA/SmartStandingDeskDualCore).
On the setup page, under *Smart desk*, enter the desk's address (for example
`smartdesk-a43c.local`) and, optionally, a desk account with the *user* role.
Without an account the memory buttons and *Stop* work; with one you also get
hold-to-raise/lower, the sit/stand timer and away mode. In the feature's
options choose which memory buttons mean *sit* and *stand* (M1 and M2 by
default). Layout C adds rules: stand for meetings of 15 minutes or more, stand
while the focus timer runs, and away mode while your phone is away. A minute
before a meeting it asks, with *Stay seated* to skip that one. The desk login
is kept on the display like the Wi-Fi password.

### Themes

Settings › Display and sound › Theme: **Midnight**, **Ocean**, **Forest**,
**Sunset**, **Paper** (light) and **High contrast**. All text meets WCAG AA
contrast in every theme.

## Firmware updates over Wi-Fi (no PC)

**Automatic, on every push to main.** Each push to `main` builds the firmware
with a new version number (`0.1.<build number>`) and publishes it as the
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
> `{"version":"0.2.0","url":"https://…/firmware.bin"}`.

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
- LVGL and LovyanGFX are compiled with `-O2`; the rest with `-Os`, so the
  firmware fits one 1.9 MB update slot (the build fails if it does not, and
  prints how much room is left).

## Project layout

```
apps-script/      Google Apps Script backend (one copy per account)
firmware/         PlatformIO project for the display
  src/            display, Wi-Fi, sync, SD cache, audio, OTA, setup page, UI
  src/fonts/      Montserrat (English characters) and icons (tools/gen_fonts.sh)
  sim/            desktop build of the UI that renders screenshots (sim/build.sh)
.github/workflows GitHub build: factory image, OTA image, releases
docs/             C4 architecture and feature flows (architecture.drawio)
```

Every source file starts with a "Texts and settings" block holding its
on-screen texts and tunable numbers, so you can change wording or timings
without reading the code.

## Architecture diagrams

`docs/architecture.drawio` opens in [draw.io](https://app.diagrams.net) or the
VS Code Draw.io extension. It has one page per level of the C4 model (system
context, containers, firmware components, Apps Script components) and one
swimlane flow per feature: start-up, Wi-Fi, adding an account, background
sync, dismissing a meeting, completing a task, week view, reminders, night
mode, themes, calendars, settings, removing an account, quick add, the focus
timer, the daily summary and review, conflicts, leave-by alerts, countdowns,
layouts, the room sensors, the meeting timer and cost, phone presence, the
procrastination radar, task roulette, the smart desk, the three update paths and the first flash. The file is generated: edit `docs/diagrams/build_drawio.py`
and run `python3 docs/diagrams/build_drawio.py`.

To check UI changes without the board, run `firmware/sim/build.sh` (needs gcc,
git and Python with Pillow); screenshots of every screen and theme land in
`firmware/sim/out/`.
