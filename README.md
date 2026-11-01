# ESP32 Display: Calendar and To-Do

A touch display for your desk. It shows your meetings from several Google
accounts (and Outlook or iCloud calendars) and your Google Tasks. You can finish
and add tasks, run a focus timer, browse the week, get reminders, and control a
smart standing desk. After the first flash, new firmware installs over Wi-Fi.

| Home | Week | Tasks |
|---|---|---|
| Today, coming up and to-do side by side | Monday to Sunday at a glance | Filters, due dates and a Done tab |

## Documentation

- **[User guide](docs/user-guide.md)**: every screen and feature, day to day.
- **[Setup guide](docs/setup-guide.md)**: flashing, Wi-Fi, the setup page, the
  Google script, other calendars, the desk, 7" and 5" differences.
- **[Developer guide](docs/developer-guide.md)**: code layout, building, CI and
  releases, memory and display notes, simulator, test harness, APIs.
- [Test plan](docs/test-plan.md) and the generated
  [architecture diagrams](docs/architecture.drawio) (open in
  [draw.io](https://app.diagrams.net)).

## Hardware

- **Elecrow CrowPanel ESP32 HMI 7.0"** (main board) or **5.0"**: ESP32-S3,
  800x480 RGB LCD, GT911 touch, 4 MB flash, 8 MB PSRAM. Each size has its own
  firmware file (`factory.bin` / `factory-5inch.bin`).
- Optional: microSD card, small speaker, LiPo with a MAX17048 fuel gauge,
  BH1750 light sensor, PIR motion sensor on IO44.

## Quick start

1. Download `factory.bin` (7") or `factory-5inch.bin` (5") from the
   [latest release](https://github.com/LimitlessA/ESP32-Display-Calendar-and-To-Do/releases/latest)
   and flash it at `0x0` with <https://espressif.github.io/esptool-js/>. If the
   screen stays black, flash again.
2. Connect the display to Wi-Fi on its own screen.
3. Put `apps-script/Code.gs` in each Google account, set `TOKEN`, deploy it as a
   web app, then add its URL and token on `http://calendar.local` (the PIN is on
   the display, under Settings > Accounts > + Add).

Details for each step are in the [setup guide](docs/setup-guide.md).

## Project layout

```
apps-script/      Google Apps Script backend (one copy per account)
firmware/         PlatformIO project (envs crowpanel_7inch and crowpanel_5inch)
  src/            firmware and UI; web/ is the setup page; sim/ the desktop UI build
.github/workflows GitHub build: factory images, OTA images, releases
docs/             guides, test plan, architecture diagrams
```

Every source file starts with a "Texts and settings" block, then "State", then
"Code". See the developer guide.
