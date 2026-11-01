# User guide

This guide is for Andrei, who uses the display every day. It describes what the
screen does today. Nothing here is a plan.

Setting the display up for the first time is in [setup-guide.md](setup-guide.md).
Changing the code is in [developer-guide.md](developer-guide.md).

Where something is not clear from the code, this guide says so. Look for
**Not sure**.

## The big picture

The display shows your meetings and your to-do list. The data comes from your
Google accounts. A small Google script in each account reads the calendar and
Google Tasks and sends them to the display.

- The display refreshes in the background. By default it asks every 3 minutes.
- It also asks right after you tap something.
- Taps work at once on the screen. The change is sent to Google a moment later.
- If Google says no, the change is rolled back and a message tells you why.

## The header and the tabs

The bar at the top is on Home, Week, Tasks, Settings, Dates and Desk.

- On the left: the clock and the date.
- A small **SD card icon** shows when the screen is showing the saved copy from
  the SD card, not fresh data. Tap it to see why.
- A **timer pill** appears while the focus timer runs. Tap it to open the timer.
- A **car pill** ("Leave in 12:30") appears if Leave-by alerts use layout C.
- On the right: the tabs. Home, Week and Tasks are always there. A **Dates** tab
  shows only with Countdowns layout C. A **Desk** tab shows only when the Smart
  desk is on and uses layout B or C. The last tab (sliders icon) is Settings.

## Home

Home has three columns.

**Today**
- Each meeting is a card with its start time, title, end time and calendar.
- The meeting you are in now has a coloured border and says "Now, until 15:00".
- Tap a card to open its details.
- Tap the **x** to hide the meeting from the display. Your Google Calendar is
  not changed.
- All-day events say "All day".
- If there is nothing left, it says "No more meetings today".

**Coming up**
- The next days, one heading per day. Tap a row to open it. Tap the **x** to hide it.
- A small warning sign marks meetings that overlap (see Conflict alerts).

**To-do**
- Your open tasks, with their due date as plain coloured text.
- Tap the **circle** to finish a task. A green tick shows, then the task leaves
  the list. A message with **Undo** stays for about 7 seconds.
- **Hold** a task to start the focus timer.
- **Add a task** (top right of the column) opens quick add.
- Subtasks are only shown on the Tasks screen.

Home can also show a countdown column or ribbon (Countdowns), a conflict banner,
a leave-by card and the desk dock. These are layout choices. See
[Features](#features-settings--features).

## Meeting details

Tap a meeting anywhere to open its details. You see the title, the time, the
place, the calendar, the organiser and the guests. If the meeting has a place
and travel times are set up, you also see "25 min travel from home. Leave by
13:35."

Buttons:
- **Back**.
- **Remind me before** / **Reminder on (5 min before)**. This switches the
  reminder for this one meeting. The choice is kept in memory only. It is
  forgotten when the display restarts.
- **Count down to this**. This stars the meeting so it shows as a countdown
  (needs the Countdowns feature).
- **Dismiss from display**. Hides the meeting here only.

If Phone presence is on in Privacy mode and your phone is away, meetings cannot
be opened and their titles read "Busy".

## Week

Monday to Sunday, one column per day.

- The arrows move one week back or forward. **This week** jumps back to now.
- Today has a coloured border. Past days are dimmer.
- Tap an event to open its details.
- A day with no meetings says "Free".
- A meeting that lasts several days shows on every day it covers.
- If a week is not loaded yet it says "Loading the week...". The current and
  next week usually load with the normal sync.
- With Conflict alerts layout C, overlapping meetings get a red outline and
  their day gets a warning sign.
- With Meeting cost layout C, a **Meeting cost** button appears (see below).

## Tasks

The left side is a list of filters. The right side shows the tasks.

Top of the left side:
- **Add task**. Opens quick add.
- **Pick for me**. Task roulette (if it is on).
- **Radar**. The radar view (only with Procrastination radar layout C).

Filters:
- **All**, **Overdue**, **Due today**, **Done**.
- Then one heading per Google account, with a button for each of its task lists.
- Each button shows how many tasks it has.

A task row has:
- A circle. Tap it to finish the task (with Undo).
- The title.
- The **due date**, written as plain coloured text: "Overdue · Fri 30 Oct"
  (red), "Today" (amber), "Tomorrow · Mon 2 Nov", "Fri 6 Nov", or "No date".
- The list, the account and the first notes, in small grey text.
- Subtasks are indented under their parent.
- **Hold** a row to start the focus timer. A hint at the bottom says so.

You cannot type a new due date on a task row. Due dates are set when you add a
task (quick add), or changed by the end-of-day review and the radar.

### The Done tab

- Shows tasks you finished in the **last 7 days**, newest first (up to 20).
- Each row has a green tick, the title crossed out, and "Completed 14:05 today",
  "Completed yesterday" or "Completed Fri 30 Oct".
- **Restore** brings the task back to your open list. The due date is kept.
- If nothing was finished, it says "Nothing completed in the last 7 days".
- Subtasks are not listed here.

## Desk

See [The desk](#the-desk) below.

## Reminders and alerts

These cover the whole screen and have a sound if **Reminder sound** is on.

**Meeting reminder**
- Appears 5 minutes before a meeting that is not all-day.
- Says "STARTS IN 5 MINUTES" (or "STARTING NOW").
- Buttons: **Snooze 5 min**, **Details**, **Got it**.
- It wakes the screen from night mode and from dimming.
- Switch all reminders with **Chime** on the Settings screen. Switch one meeting
  in its details.

**Leave-by alert** (needs travel times)
- A warning "LEAVE IN 10 MIN" and then "LEAVE NOW", before a meeting that has a
  place.
- Layout A is a full-screen alert. B is a card on Home. C is a countdown in the
  header.
- Buttons on the full-screen alert: **Details**, **Got it**.

**Conflict alerts**
- A: a banner on Home ("2 meetings overlap") with **Compare**.
- B: a popup that shows both meetings, with **Hide this one** and **Keep both**.
- C: overlaps are marked in the Week view.
- Works across all your accounts.

**Messages (toasts)**
- A short message near the bottom: "Done: Send invoice" with **Undo**, "Hidden
  from the display. Your calendar is unchanged.", sync errors, and so on.

**Firmware update window**
- Opens when an update installs. Shows progress. The screen goes dark while it
  writes, then the display restarts.

## Quick add task

Open it with **Add a task** on Home or **Add task** on Tasks.

- Type the title. The keyboard has letters, capitals, numbers and symbols.
- Pick **Due**: Today, Tomorrow, This weekend or No date. Today is the default.
- If you have several task lists, tap the list chip to change the list.
- Suggestions come from tasks you finish often ("from past tasks").
- Add the task with **Add**, or the tick on the keyboard.
- A message says "Added: ...".

Three layouts: A is a keyboard. B shows tiles of frequent tasks (and "Type my
own"). C is a sheet over the current screen with suggestions above the keyboard.

## Focus timer

- **Hold a task** on Home or Tasks to start it. The default is 25 minutes.
- A message says "Focus timer: 25 min. Tap the timer at the top to open it."
- Buttons: **Hide**, **Pause** / **Resume**, and **Stop**.
- When time is up you can pick **Mark task done**, **+5 min** or **Not yet**.
- A chime plays at the end if sound is on.
- A: a big ring. B: a split screen with big digits. C: only the header pill.
- Change the length in Features > Focus timer.

## Task roulette

Can't decide? On Tasks, tap **Pick for me**.

- The display picks one of your open tasks.
- Overdue and due-today tasks come up more often. Tasks you keep moving too.
- A: a wheel (tap **Spin**). B: a deck of cards (**Shuffle again**, 2 reshuffles).
  C: a slot machine with a task, a time box and a reward (**Pull again**,
  **Deal, start now**).
- You can **Start focus timer** on the pick, or close it.

## Daily summary

- Shows once each morning, at 07:00 by default.
- It shows up to 2 hours late if the screen was busy or in night mode.
- A: tiles (Meetings, First at, Due today, Overdue). B: a timeline from 08:00 to
  20:00. C: a short written briefing.
- Tap **Start my day** to close it.
- Change the time in Features > Daily summary (05:00 to 11:00).

## End-of-day review

- Shows once each evening, at 20:00 by default, but only if tasks are still open.
- A: a checklist. You can move each task to **Tomorrow**, or use **Move all to
  tomorrow**, then **Finish**.
- B: one task at a time with **Done**, **Tomorrow**, **Next week** and **Skip**.
- C: plan by day. Pick a day for each task (tomorrow, the next three days, or
  no date).
- Moving a task changes its due date in Google Tasks right away.
- Change the time in Features > End-of-day review (16:00 to 23:00).

## Meeting timer and meeting cost

**Meeting timer**
- While you are in a meeting, the display shows how much is left.
- A: a progress bar on the meeting card. B: a full-screen countdown. C: a glowing
  line along the top edge with a pill.
- A soft chime plays 5 minutes before the end.

**Meeting cost** (off by default)
- Counts people times the hourly rate times the time. The rate is 45 euro per
  person per hour by default. Change it in the feature's options.
- A: a line on the meeting card. B: a big counter. C: a weekly "receipt" from the
  Week screen.
- "People" is the number of guests (the organiser is not counted), and at least 1.
  **Not sure** whether this is what you expect for meetings you organise.

## Procrastination radar

- The display remembers how often a task's due date was moved or missed.
- A task counts as **stuck** after 3 moves by default (2 to 10 in the options).
- A: stuck tasks glow and show a count like "6x" and "Moved 6 times since 12 Oct".
- B: once a day, when the screen has been idle for a minute on Home, a popup
  says "YOU KEEP MOVING THIS ONE". You pick **Do 5 minutes now** (starts a
  5-minute focus timer), **Split it** (opens quick add), **Pick a fixed day**
  (Tomorrow, Saturday or Next Monday) or **Drop it**, or **Not now**.
- C: a **Radar** button on Tasks shows tasks as bubbles. Closer to the middle
  means more moves. Tap one to deal with it.
- **Drop it finishes the task in Google Tasks.** There is an Undo.
- The counts are saved on the SD card. **Not sure** what happens without a card.
  The code only writes them to the card, so they are probably lost when the
  display restarts.

## Countdowns, birthdays and holidays

- **Countdowns**: star a meeting in its details ("Count down to this").
- **Birthdays and holidays**: birthdays come from Google Contacts. Public
  holidays come from a Google holiday calendar. The Google script sets which
  one (Romanian holidays by default). It shows the next 60 days.
- A: a column on Home. B: a ribbon under the header. C: a **Dates** tab.
- The layout is one choice shared by both features.
- On Home the Dates column or ribbon is empty until something is starred or a
  date is near. It then says "Star a meeting to count down to it".

## Phone presence (Bluetooth)

The display notices when your phone is near.

1. Turn on Settings > Features > Phone presence.
2. **The first time, the display restarts once** to start Bluetooth. This is
   expected.
3. Hold the tile, then tap **Pair a phone**.
4. On the phone, open Bluetooth settings and pick **Calendar display**. Accept
   the pairing. You have 2 minutes.
5. The display shows "Paired!".

The phone shows up as a media remote with no keys. It never acts as a keyboard.
Nothing is sent to the phone.

- You are away after 10 minutes without your phone (5 to 60 in the options).
- iPhones stay connected reliably. Some Android phones drop the link to save
  battery, so allow a longer away time.
- **Forget** removes the pairing on the display. Remove "Calendar display" on the
  phone too.
- A: "Welcome back" summary of what changed (only after you were away for 15
  minutes or more), with **Show my day**.
- B: Privacy mode. While you are away, meeting titles read "Busy" and meetings
  cannot be opened.
- C: Away screen. The dim night clock shows until you are back.
- If there is not enough free memory, the display says "Bluetooth could not
  start: only N KB of internal memory free".

## Ambient light

- Needs a BH1750 light sensor. Without one, the tile says "Needs a BH1750 light
  sensor on I2C" and cannot be switched on.
- With it on, the screen gets dimmer in a dark room. The Brightness slider is the
  top level, in a bright room.
- The sensor is looked for when the display starts. Restart after you plug it in.

## Motion wake

- Needs a PIR motion sensor on IO44.
- With it on, the screen wakes when someone walks up, as if you had touched it.
- IO44 is also the serial port. Unplug the sensor when you flash over USB.
- The display does not check that a sensor is there. The tile can be switched on
  without one.

## The offline copy on the SD card

See [The SD card](#the-sd-card).

## The night clock

- Between 22:00 and 07:00 (the default) the screen goes dark with a big clock.
- It also shows the date, the next meeting, and how many tasks are due, and
  "Night mode until 07:00 · tap to wake".
- It starts after 30 seconds without a touch. It never starts while a pop-up is
  open.
- Touch the screen to wake it. It goes back to the clock 30 seconds later.
- A meeting reminder wakes the screen by itself.
- Switch it with **Night mode** on the Settings screen.
- **Not sure** how to change the hours. The Settings screen shows them (22:00 -
  07:00) but there is no control for them on the display or on the setup page.
  They are fixed in the code.
- With Phone presence layout C, the same clock shows when your phone is away.

## Settings

Open it with the sliders icon. It is a dashboard.

**Three tiles at the top.** Tap one to open more.
- **WI-FI**: network name, signal (Strong, Good, Weak) and IP address. Opens the
  Wi-Fi screen.
- **ACCOUNTS**: how many are connected, and when it last synced, or what needs
  attention. Opens Accounts.
- **DEVICE**: battery (if there is a fuel gauge), free SD card space or "No SD
  card", and "fw 0.2.N · Update". Opens the firmware update window.

**Two sliders.**
- **Brightness**: 10% to 100%.
- **Volume**: 0% to 100%. It plays a chime when you let go.
- Each slider shows its lowest and highest value at the ends.

**Four toggle chips.**
- **Night mode** (shows the hours).
- **Chime** (shows "5 min before").
- **Dim idle** (shows "after 2 min"). Dims the screen when nobody touches it.
- **Auto-update** ("when idle").

**Bottom row.**
- **Sync**: a - and + stepper for 2, 3, 5, 10, 15 or 30 minutes, and a button to
  sync now.
- **Time**: 24 h or 12 h.
- **Theme**: opens the theme picker. Midnight, Ocean, Forest, Sunset, Paper (a
  light theme) and High contrast.
- **Features**: "N on". Opens the Features screen.

Settings are saved about 5 seconds after your last change. Wait a few seconds
before you pull the power.

### The Wi-Fi screen

Settings > WI-FI. It lists networks nearby ("Scan again" to refresh). Tap one,
type the password on the keyboard, and tap **Connect**. The network is saved
only if the connection works. If it fails: "Could not connect. Check the
password and try again."

## Features (Settings > Features)

A grid of 18 cards. Every card shows **ON** or **OFF**, the feature name, and its
current setting.

- **Tap** a card to turn the feature on or off.
- **Hold** a card to open its options.

In the options window you can:
- Switch the feature on or off.
- Change its number with the - and + buttons (a time, minutes, a rate, or a
  count).
- Pick its layout: **A**, **B** or **C**.
- Tap **Done**.

The cards:

| Card | Option | Layouts (A / B / C) |
|---|---|---|
| Daily summary | time, 05:00-11:00 | Stat tiles / Timeline / Briefing |
| End-of-day review | time, 16:00-23:00 | Checklist / One at a time / Plan by day |
| Conflict alerts | none | Banner on Home / Compare popup / Marked in Week |
| Leave-by alerts | warn 5-60 min | Full screen / Card on Home / Header countdown |
| Meeting timer | none | Card progress / Full screen / Glowing edge |
| Meeting cost | rate, 5-500 | On the card / Big counter / Weekly receipt |
| Reminder sound | none | none |
| Focus timer | length, 5-90 min | Big ring / Split / Header strip |
| Quick add task | none | Keyboard / Frequent tiles / Sheet over list |
| Procrastination radar | stuck after 2-10 moves | Glowing tasks / Nudge popup / Radar view |
| Task roulette | none | Wheel / Card deck / Slot machine |
| Countdowns | none | Home column / Ribbon / Dates tab |
| Birthdays and holidays | none | same layout as Countdowns |
| Ambient light | none | none |
| Motion wake | none | none |
| Phone presence | away after 5-60 min | Welcome back / Privacy mode / Away screen |
| Smart desk | sit and stand memory, desk login | Dock on Home / Desk screen / Follows calendar |
| Offline copy | none | none |

Defaults: Ambient light, Motion wake, Phone presence, Meeting cost and Smart
desk are **off**. The others are on.

The default layouts are Quick add B, Focus A, Summary A, Conflicts A, Leave-by B,
Dates B, Review C, and A for everything else.

Changing a feature rebuilds the screens, so the picture may blink once.

## Settings > Accounts

A list on the left, details on the right.

**Left list** (three groups):
- **GOOGLE**: your accounts. Each shows its name and its status.
- **CALENDARS**: published calendar links (Outlook, iCloud and so on).
- **DESK**: the smart desk.
- **+ Add account / calendar / desk** at the bottom.

**Google account details**: Account (email), Status, Calendars / tasks (how many
calendars are shown, and tasks), Colour. Buttons:
- **Calendars**: choose which calendars of that account show on the display.
- **Sync now**.
- **Remove**. Asks first. Nothing is deleted in Google.
- If the account has a problem, a **Fix on calendar.local** button shows instead
  of Calendars.

**Calendar link details**: Link (the website name), Status ("12 events, synced"),
Read by (the first Google account), Colour. Buttons: **Sync now**, **Remove**.
It needs a Google account because that account's script reads the link.

**Desk details**: Address, Login, Status. Buttons: **Test connection**, **Edit
login**, **Forget**.

**+ Add** shows a QR code, the address of the setup page, and the 6-digit PIN.
You add accounts, links and the desk there. See the setup guide.

## The desk

This needs a standing desk running SmartStandingDeskDualCore. Turn on
**Smart desk** in Features. Set the address and login (see
[Desk settings](#desk-settings-and-login)).

Without a desk login, only the **memory buttons** and **Stop** work. With a
login you also get **hold to raise or lower**, the **sit/stand timer** and away
mode. The login needs the desk's **user** role.

The three layouts:
- **A, Dock on Home**: a bar along the bottom of Home. It shows the height and
  state, a timer button, M1 to M4, two arrow buttons (with login) and **Stop**
  (which covers the memory buttons while the desk moves).
- **B, Desk screen**: a **Desk** tab. A drawn desk that moves with the real one,
  the height, the four memory buttons, **Hold to raise** and **Hold to lower**,
  **Stop**, the timer card, and a **Desk settings** button.
- **C, Follows calendar**: the Desk screen plus rules (below) and a timeline of
  your day.

**Memory buttons M1 to M4**
- Tap one and the desk goes to that position.
- They are always labelled M1 to M4. The ones you chose for sit and stand also
  say "Sit" or "Stand". Default: M1 sit, M2 stand. Change this in the Smart desk
  options.
- The height under each button needs the desk login.

**Hold to raise or lower**
- Press and hold. The desk moves while your finger is down and stops when you
  let go.
- Only with a desk login.
- **Not sure** how fast the desk stops on its own if the display loses contact
  while you hold. The display comment says about half a second. The desk's own
  setting (`hold_timeout_ms`) is 10 seconds by default. Let go to stop it.

**Stop** stops the desk at once. It needs no login.

**The ring timer (sit/stand)**
- Only with a desk login. It is the desk's own timer. Tap the play triangle in
  the ring to start.
- Before it runs, set the **Sit** and **Stand** minutes with - and + (5 to 90).
  If you did not set them, the lengths saved on the desk are used.
- While it runs, the ring counts down the minutes and says "Sitting" or
  "Standing", and "then Stand 15". In the last minute it counts seconds.
- **Possibly broken**: the display reads the desk's phase as "SIT" or "STAND",
  but the desk (version read for this guide) says "SITTING" or "STANDING". If
  that is still true on your desk, the ring may always say "Standing" and
  "Sit in N min", even in the sit phase. Check this on the real desk.
- Buttons: pause / resume, and stop.
- The timer starts at the desk's sit and stand heights (memory buttons you
  chose).
- The desk may refuse to start it outside its own working hours. The display
  does not show a message for that. **Not sure**: this comes from the desk, not
  the display.

**Rules (layout C)**
- **Stand for meetings of 15 min or more** (on by default). A minute before such a
  meeting a popup says the desk will stand up. Press **Stay seated** to skip it.
  When the meeting ends the desk goes back to sitting.
- **Stand during focus timers** (off by default).
- **Away mode when your phone leaves** (on by default). Needs Phone presence.
- The timeline also shows today's standing time and the number of changes.

### Desk settings and login

Open it from the Smart desk options ("Desk address, login and connection test"),
from the **Desk settings** button on the Desk screen, from Settings > Accounts >
Desk > **Edit login**, or from the setup page.

- **Address**: for example `smartdesk-a43c.local`, or an IP address.
- **Username** and **Password**. Leave the username empty for guest mode.
- **Test connection** tries it before you save, and says what it found.
- **Save**.

The password is kept on the display, like the Wi-Fi password.

## Firmware updates

**Automatic**
- Every push to the `main` branch of the project builds new firmware and
  publishes it as a GitHub release.
- The display looks for a new release one minute after start and every 3 hours.
- It installs by itself only when nobody is using it: during night mode, or after
  15 minutes without a touch. Never in the 15 minutes before a meeting.
- The update window shows progress. The screen goes dark while it writes. Then
  the display restarts. Settings and accounts are kept.
- Turn this off with **Auto-update** on the Settings screen.
- The repository must be public, because the display downloads without signing
  in.

**By hand, on the display**
- Settings > DEVICE tile. It checks and tells you "Version X is available". Tap
  **Install**.
- It will not install an older or equal version.

**By hand, from a file**
- On the setup page, Device tab, **Install by hand**. Choose a `firmware.bin`
  (not `factory.bin`), enter the PIN, and press **Upload and install**.
- Use the file made for your board. See the setup guide for the 7" and 5" files.
  **Not sure**: the display does not check that the file is for your board. A
  wrong file is installed anyway, and the picture will show lines and flicker.
  Flash the right file again over USB.

If an update fails, the window shows the error and the address of the setup page.

## The SD card

An SD card is optional. Everything works without it.

With a card the display keeps:
- **An offline copy** of each account's last feed (Features > Offline copy). After
  a restart or when the internet is down, the display shows this copy at once. The
  SD badge in the header says so.
- **A backup of your settings** in `/calendar/settings.json`, including Wi-Fi and
  account details. If the flash is erased (for example by a clean reflash), the
  display reads it back at the next start.
- **The radar counts** (`/radar.txt`).

The Settings DEVICE tile shows free space or "No SD card".

The settings backup is plain text. Keep the card to yourself.

## If something looks wrong

- **Header shows the SD icon**: the data is the saved copy. Tap the icon for the
  reason. Check Wi-Fi and the Accounts screen.
- **Accounts tile says "N needs attention"**: open Accounts, pick the account,
  read the Status, and use **Fix on calendar.local**.
- **Screen stays black after flashing**: flash again. See the setup guide.
- **Lines or flicker on the picture**: the firmware is for the other board size.
- **Nothing changes after you edit a setting and cut the power**: settings save 5
  seconds after the last change.
