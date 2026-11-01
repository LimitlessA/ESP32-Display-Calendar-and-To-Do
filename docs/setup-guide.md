# Setup guide

This guide takes you from a new board to a working display. You need:

- An Elecrow CrowPanel ESP32 HMI, **7.0 inch** or **5.0 inch**, and a USB-C cable.
- Chrome or Edge on a computer.
- A Google account for each calendar and task list you want on the display.
- Your home Wi-Fi name and password.
- Optional: a microSD card, a small speaker, a light sensor, a motion sensor, a
  battery fuel gauge. See the [README](../README.md) for the parts.

Day-to-day use is in [user-guide.md](user-guide.md).

The steps, in order:

1. [Flash the first firmware](#1-flash-the-first-firmware)
2. [Connect to Wi-Fi](#2-connect-the-display-to-wi-fi)
3. [Open the setup page](#3-open-the-setup-page)
4. [Deploy the Google script for each account](#4-deploy-the-google-script-for-each-account)
5. [Add the accounts to the display](#5-add-the-accounts-to-the-display)
6. [Add Outlook, iCloud or other calendars](#6-add-outlook-icloud-or-other-calendars-optional)
7. [Connect the smart desk](#7-connect-the-smart-desk-optional)

## 1. Flash the first firmware

You do this once, over USB. After that, updates come over Wi-Fi.

### Pick the right file for your board

The two boards wire the screen differently. Use the file for your board.

| Board | Full image for the first flash | App-only image (updates) |
|---|---|---|
| CrowPanel 7.0 inch (the main board) | `factory.bin` | `firmware.bin` |
| CrowPanel 5.0 inch | `factory-5inch.bin` | `firmware-5inch.bin` |

A 5 inch file on a 7 inch board (or the other way round) shows lines and flicker.

### Get the file

- **Releases.** Every push to `main` makes a release. The newest one is here:
  `https://github.com/LimitlessA/ESP32-Display-Calendar-and-To-Do/releases/latest`.
  Download `factory.bin` (7 inch) or `factory-5inch.bin` (5 inch) from its
  Assets.
- **Actions.** Open the repository's **Actions** tab, click the latest green
  **Build firmware** run, and download the **firmware** artifact. Unzip it.
- **A test build of another branch.** The build also keeps the files of every
  other branch on a Git branch called `builds/<branch name>`. Fetch it with
  plain git.

`factory.bin` is the whole flash image: bootloader, partition table and app in
one file. It must go to address `0x0`.

### Flash it in the browser

1. Connect the board to your computer with the USB-C cable.
2. In Chrome or Edge, open <https://espressif.github.io/esptool-js/>.
3. Set the baud rate to **921600** and press **Connect**. Pick the board's serial
   port (often "USB Serial" or "CH340").
   - If it does not connect: hold **BOOT**, tap **RESET**, release **BOOT**, and
     try again.
4. Under **Flash Address** enter `0x0`. Choose the right `factory` file. Press
   **Program**.
5. When it says it is done, press **RESET** on the board.

**Tip: a browser flash can be incomplete.** If the screen stays black after the
reset, flash again. Do the same steps. It usually works the second time.

### Flash with PlatformIO instead

If you build the code yourself, see the [developer guide](developer-guide.md).

## 2. Connect the display to Wi-Fi

1. After the first start, the display opens the **Wi-Fi** screen by itself. It
   scans for networks.
2. Tap your network. Type the password on the on-screen keyboard.
3. Tap **Connect**. The display says "Connected to <name>" and shows its IP address.

The network is saved only if the connection works. If it fails, it says "Could
not connect. Check the password and try again."

Later, you change the network in **Settings > WI-FI**. The setup page cannot
change it.

The display sets its clock from the internet (NTP) once Wi-Fi works. Until then,
the clock says "--:--".

## 3. Open the setup page

The setup page is a small website on the display. You use it to add accounts,
calendar links and the desk.

1. On the display, open **Settings > ACCOUNTS**, then **+ Add account / calendar /
   desk**.
2. A window shows a **QR code**, the address, and a **6-digit PIN**.
3. Scan the QR code with your phone. Or type the address on any device on the
   same Wi-Fi. The address is `http://calendar.local`, or the display's IP
   address, such as `http://192.168.1.42`.
   - The QR code uses the IP address, because `.local` names do not work on every
     phone.

**The PIN**
- The PIN is shown only on the display.
- **It changes every time the display restarts.** Read it again after a reboot.
- On the page, type the PIN and press **Unlock**. It stays unlocked until you
  leave the page, and you can press **Lock** to lock it.
- Five wrong PINs lock changes for one minute.
- Anyone on your Wi-Fi can open the page and see the status (account names and
  e-mail addresses, the Wi-Fi name, the desk address). Nobody can change anything
  without the PIN. Tokens and passwords are never sent to the page.

**The tabs**

| Tab | What is on it |
|---|---|
| **Accounts** | Google accounts: add and remove. Outlook, iCloud and other ICS calendars: add and remove. Each has a "how to" box. |
| **Desk & sync** | Smart desk: address, user, password, Save, Test connection. Sync interval: 2, 3, 5, 10, 15 or 30 minutes. |
| **Device** | Wi-Fi status (read only). Firmware: the update link and the installed version. Install by hand: upload a `firmware.bin`. |

The page works on a phone and on a computer.

## 4. Deploy the Google script for each account

Do this once for **each** Google account you want on the display. Each account
has its own copy of the script, its own web app URL, and its own token.

1. Sign in to the Google account. Open <https://script.google.com/create>.
2. Replace the contents of `Code.gs` with `apps-script/Code.gs` from this
   repository.
3. Open **Project Settings**. Tick **Show "appsscript.json" manifest file in
   editor**. Replace that file with `apps-script/appsscript.json`. It switches
   on the Tasks and Calendar services.
4. In `Code.gs`, find `CONFIG`. **Set `TOKEN`.** Change `TOKEN: 'change-me'` to a
   long random password. You will paste the same value on the setup page. Keep it
   private.
   - For leave-by alerts, set `HOME_ADDRESS` to where you leave from. Set
     `TRAVEL_MODE` to `driving`, `walking`, `bicycling` or `transit`.
   - For public holidays of another country, change `HOLIDAYS_CALENDAR`. The
     default is Romania (`en.romanian#holiday@group.v.calendar.google.com`).
5. In the function list, pick **`preview`** and press **Run**. Allow access when
   Google asks. The log shows what the display will receive.
6. Press **Deploy > New deployment > Web app**. Set **Execute as: Me** and
   **Who has access: Anyone**. Press **Deploy**.
7. Copy the **Web app URL**. It ends in `/exec`.

The "Anyone" setting is needed because the display does not sign in. The token
is what protects the script. Use a long one.

### When Code.gs changes later

1. Paste the new version of `Code.gs` over the old one. **Keep your `CONFIG`
   values** (especially `TOKEN`).
2. Press **Deploy > Manage deployments**.
3. Edit the web app (pencil icon). Under **Version**, pick **New version**.
   Press **Deploy**.

The URL stays the same. **Saving alone is not enough.** The display talks to the
last deployed version, not to the code you see in the editor.

## 5. Add the accounts to the display

On the setup page, **Accounts** tab, **Google accounts**:

1. **Name shown on the display**: for example "Personal" (up to 20 characters).
2. **Web app URL**: the `/exec` link from step 4. It must start with
   `https://script.google.com/` and end with `/exec`.
3. **Token**: the `TOKEN` you set.
4. Press **Add Google account**.

Unlock the page first with the PIN. You can add up to 6 accounts.

The meetings and tasks appear within a few seconds. Check **Settings > ACCOUNTS**
on the display. A green "Synced" means it works. If an account says "needs
attention", open it in Accounts and read the Status.

Common errors:
- `unauthorized`: the token on the page is not the `TOKEN` in the script.
- `bad reply: ...` followed by the title of a Google page: the web app is not
  shared as "Anyone", or you did not deploy a **new version** after changing the
  code.
- `HTTP` and a number: check the URL.

## 6. Add Outlook, iCloud or other calendars (optional)

You can add calendars from other services with a published **ICS link**.

On the setup page, **Accounts** tab, **Outlook, iCloud & other (ICS)**:

1. **Name shown on the display**.
2. **Calendar link (ICS)**. It starts with `https://`, `http://` or `webcal://`.
3. Press **Add calendar**. You can add up to 6.

**Where to find the link**
- **Outlook**: Settings > Calendar > Shared calendars > **Publish a calendar**.
  Copy the **ICS** link.
- **iCloud**: in the Calendar app, share the calendar as a **Public Calendar**
  and copy the link.
- **Other**: any link ending in `.ics`, or starting with `webcal://`.

**Important**
- The **first Google account's script reads these links**. Add a Google account
  first. If you remove that account, the next one takes over.
- The script re-reads a link at most every 10 minutes.
- Another way is to add the link in Google Calendar under **Other calendars >
  From URL**. Google refreshes those only every few hours, so the setup page
  route is faster.
- A published link is public to anyone who has the address. Treat it like a
  password.

## 7. Connect the smart desk (optional)

This is for a desk running
[SmartStandingDeskDualCore](https://github.com/LimitlessA/SmartStandingDeskDualCore).

1. Make sure the desk and the display are on the same network.
2. On the setup page, **Desk & sync** tab, **Smart desk**:
   - **Desk address**: for example `smartdesk-a43c.local`, or an IP address.
   - **Desk username**: optional. Use an account with the **user** role on the
     desk. Leave it empty for guest mode.
   - **Desk password**: leave it empty to keep the saved one.
3. Press **Save desk**.
4. Press **Test connection**. It tells you if it found the desk, and if the login
   was accepted. You can test before you save.
5. On the display, open **Settings > Features**, hold the **Smart desk** card, and
   switch it on. Pick the layout, and which memory buttons mean sit and stand.

You can also set the address and login on the display, in the Smart desk options.

Without a login, only the memory buttons and Stop work. With a login you also
get hold to raise and lower, the sit/stand timer and away mode.

The default address in the code is `smartdesk-a43c.local`. It is only a default.
Use your desk's own name.

## The 7 inch and the 5 inch boards

Most things are the same. The differences:

| | 7.0 inch (main) | 5.0 inch |
|---|---|---|
| Firmware files | `factory.bin`, `firmware.bin`, `manifest.json` | `factory-5inch.bin`, `firmware-5inch.bin`, `manifest-5inch.json` |
| Build command | `pio run -e crowpanel_7inch` | `pio run -e crowpanel_5inch` |
| Screen wiring | its own pins | different pins for colour lines, DE and VSYNC |
| Screen speed | about 49 frames per second | about 35 frames per second |
| Touch start-up | uses a small I/O chip to reset the touch controller | no such chip |
| Pins for SD card, speaker, I2C, motion sensor | same | same |
| Screen size in the UI | 800 x 480 | 800 x 480 |

Notes:
- The picture is the same size on both boards. The firmware draws 800 x 480 on
  both.
- Each board checks its own manifest for updates, so a 7 inch display never
  installs a 5 inch image by itself.
- Uploading a file by hand is not checked. Use the right one.
- **Not sure** about the SD card. The hardware description says the TF card slot
  is on both boards, and the pins are the same. In Andrei's test setup the 5 inch
  board has a card and the 7 inch board does not.

## Troubleshooting

- **Black screen after the browser flash**: flash again.
- **Browser cannot find the board**: try another USB cable (some are charge-only).
  Hold BOOT, tap RESET, release BOOT.
- **Lines or flicker**: you flashed the file for the other board.
- **`calendar.local` does not open**: use the IP address. It is on the Wi-Fi
  screen and on the Settings WI-FI tile.
- **Wrong PIN**: the PIN changed after a restart. Read it from the display again.
- **Motion sensor on IO44 and flashing fails**: IO44 is the serial port. Unplug
  the sensor while you flash.
