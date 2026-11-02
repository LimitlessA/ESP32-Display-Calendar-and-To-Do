# Test plan - CrowPanel calendar display

Run on both boards after every merge of fix branches: 7.0" on COM7, 5.0" on COM16
(5" has a TF card, 7" has none). Everything is driven from the PC with the
built-in harness (`firmware/tools/harness.py`), so each step leaves a screenshot.

## Setup

1. Build both boards (`pio run -e crowpanel_7inch -e crowpanel_5inch`, from PowerShell,
   in a path without spaces) and check the real size line: `firmware.bin: N of 1966080`.
2. Flash both over USB (`esptool write-flash 0x10000 firmware.bin`).
3. Harness check on each port: `cmd heap`, `cmd pin`, `shot boot.png`.
4. Record per board: firmware version (Settings), free internal/PSRAM (`heap`) at idle.

Conventions: `H` = harness command, `S` = screenshot to keep. A step passes when the
screenshot shows the expected screen, the board did not reset (no boot banner on the
port) and `heap` stays above 30 KB internal free. Any panic: save the log, decode with
`harness.py decode --env <env> "<Backtrace line>"`.

## 1. Regression list (Andrei, 1 Nov)

| # | Check | How |
|---|-------|-----|
| R1 | 5" with TF card: card found, offline copy and settings backup written | boot log, Settings shows SD free space |
| R2 | Every slider shows its start and end value | S of each settings/feature screen with a slider |
| R3 | Weather icon stays visible on Home | S of Home now, after 1 min and after a sync |
| R4 | Calendar and tasks refresh at the set interval (and sooner after a change) | change an event on the phone, time until it shows |
| R5 | Hourly rate, desk memory slots and similar values go up and down (no cycle-only buttons) | tap both directions, S |
| R6 | Desk settings (host, login) reachable on the display and the setup page; Settings > Accounts opens | S |
| R7 | Desk timer can be edited | S before/after |
| R8 | Setup page usable on phone (390 px) and desktop (1280 px) | browser screenshots at both widths |
| R9 | Week view loads quickly once an account is connected | time from tapping Week to filled days |
| R10 | Other calendars (Outlook/ICS link) can be added and show events | add an ICS URL, events appear |
| R11 | Roulette ("spin") window has a way back | tap back, S |
| R12 | Done tasks are listed and can be brought back | complete a task, open done list, restore it |
| R13 | Due dates show as plain text, not in a rounded box | S of Tasks and Home to-do |
| R14 | Turning on phone presence does not reset the display | toggle, watch port for 60 s |
| R15 | Quick add: Today / Tomorrow do not reset the display | tap each, watch port |
| R16 | Adding and completing tasks gives no API error | add, complete, undo; check toast and log |

## 2. Screens

For each: `H screen <name>`, S, then tap every button on it once (back to the screen
after each). Names: home, detail, week, tasks, night, wifi, accounts, settings,
features, dates, desk.

- Home: clock, date, countdown chips, Today / Coming up / To-do columns, Add a task,
  tap an event (opens Detail), tap a task circle (completes, undo toast).
- Detail: event title, time, location, calendar colour, star / dismiss, back.
- Week: previous / This week / next, today highlighted, events per day, tap an event.
- Tasks: lists, due dates, complete / undo, move to tomorrow / next week, hold to start focus.
- Night: clock only, dim; tap wakes to Home.
- Wi-Fi: scan list, password keyboard, connect, wrong password message.
- Accounts: list, colours, setup page address and PIN, remove.
- Settings: every row opens or toggles; brightness and volume sliders take effect.
- Features: every switch toggles and persists after reboot (settings are saved 5 s
  after the last change: wait 6 s, `reset`, check).
- Dates: birthdays and holidays list.
- Desk: sit / stand, memory slots, timer, status, offline message when the desk is absent.

## 3. Features (Settings > Features), each on and off

Sound, morning summary (time), evening review (time), conflicts, leave-by (lead
minutes), focus timer (minutes), quick add, countdowns, dates, ambient light (needs
BH1750: expect "not present"), motion (needs PIR; note: claims the serial RX pin, so do
this one last and expect the harness to stop reading until reboot with it off),
offline copy (5" only), meeting timer, presence (pair a phone; away minutes), cost meter
(hourly rate), radar (moves), roulette, desk (host, login, sit/stand slots, meetings /
focus / away automation).

For time-based features (summary, review, reminders, leave-by, meeting timer) set the
time in the feature to two minutes ahead and wait for the overlay. S of each overlay.

## 4. Themes and layouts

- Themes: Midnight, Ocean, Forest, Sunset, Paper. Switch each, S of Home, Week, Tasks,
  Settings (Paper is the light one: check contrast).
- Layouts A/B/C for: Quick add, Focus, Summary, Conflicts, Leave-by, Dates, Review,
  Features, Meeting timer, Presence, Cost, Radar, Roulette, Desk. S of each variant.

## 5. Sync and accounts

- Google (Apps Script): feed loads, week range loads, complete / uncomplete, add task
  (with and without due date, Today / Tomorrow), move due date, star / dismiss event,
  show / hide calendar. Watch the log for HTTP or TLS errors; `heap` during a sync.
- ICS / Outlook link: add, events appear in Home and Week, remove.
- No network: offline message, last copy from the TF card (5").
- Romanian letters (Ă Â Î Ș Ț, cedilla forms) in event and task titles render.

## 6. Updates (OTA)

1. Setup page upload of the other board's `firmware.bin` is refused or flagged; the right
   one installs.
2. Upload via `/update` with the PIN: screen goes dark while writing, display restarts,
   version shows, settings kept.
3. GitHub check: Settings > Check for update with a newer release available; installs,
   no downgrade to a lower version.
4. During each update: no shift or flicker before the screen goes dark.

## 7. Stability

- 30 min idle on Home with sync running: no reset, `heap` minimum stays above 20 KB.
- 10 minutes of fast screen switching (`screen` loop) and taps: no reset, no shift.
- Power cycle: settings, Wi-Fi and accounts kept.
