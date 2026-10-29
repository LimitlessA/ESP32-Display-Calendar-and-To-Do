#!/usr/bin/env python3
"""Builds docs/architecture.drawio: the C4 model (context, containers,
components) and one flow diagram per feature.

Edit the pages below and run:  python3 docs/diagrams/build_drawio.py
"""
import os
from xml.sax.saxutils import escape

OUT = os.path.join(os.path.dirname(__file__), "..", "architecture.drawio")

# ---- Styles (C4 colours) -------------------------------------------------------

PERSON = "rounded=1;arcSize=30;whiteSpace=wrap;html=1;fillColor=#08427B;strokeColor=#073B6F;fontColor=#FFFFFF;"
SYSTEM = "rounded=1;arcSize=10;whiteSpace=wrap;html=1;fillColor=#1168BD;strokeColor=#0B4884;fontColor=#FFFFFF;"
EXTERNAL = "rounded=1;arcSize=10;whiteSpace=wrap;html=1;fillColor=#8C8496;strokeColor=#736782;fontColor=#FFFFFF;"
CONTAINER = "rounded=1;arcSize=10;whiteSpace=wrap;html=1;fillColor=#438DD5;strokeColor=#3C7FC0;fontColor=#FFFFFF;"
STORE = "shape=cylinder3;boundedLbl=1;size=12;whiteSpace=wrap;html=1;fillColor=#438DD5;strokeColor=#3C7FC0;fontColor=#FFFFFF;"
COMPONENT = "rounded=1;arcSize=10;whiteSpace=wrap;html=1;fillColor=#85BBF0;strokeColor=#78A8D8;fontColor=#000000;"
BOUNDARY = ("rounded=1;arcSize=2;whiteSpace=wrap;html=1;dashed=1;dashPattern=8 4;fillColor=none;strokeColor=#666666;"
            "fontColor=#333333;verticalAlign=bottom;align=left;spacingLeft=10;spacingBottom=6;fontSize=12;")
REL = ("endArrow=blockThin;endFill=1;html=1;rounded=0;strokeColor=#707070;strokeWidth=1.5;fontColor=#404040;"
       "fontSize=11;labelBackgroundColor=#FFFFFF;")
TITLE = "text;html=1;align=left;verticalAlign=top;fontSize=20;fontStyle=1;fontColor=#222222;"
NOTE = "text;html=1;align=left;verticalAlign=top;fontSize=12;fontColor=#555555;whiteSpace=wrap;"

LANE = ("swimlane;startSize=34;horizontal=1;html=1;fillColor=#F3F6FA;strokeColor=#B8C4D2;fontStyle=1;fontSize=13;"
        "fontColor=#1F3550;swimlaneFillColor=#FAFBFD;")
STEP = "rounded=1;arcSize=14;whiteSpace=wrap;html=1;fillColor=#DAE8FC;strokeColor=#6C8EBF;fontSize=12;"
DECIDE = "rhombus;whiteSpace=wrap;html=1;fillColor=#FFF2CC;strokeColor=#D6B656;fontSize=11;"
START = "ellipse;whiteSpace=wrap;html=1;fillColor=#D5E8D4;strokeColor=#82B366;fontSize=12;fontStyle=1;"
END = "ellipse;whiteSpace=wrap;html=1;fillColor=#F8CECC;strokeColor=#B85450;fontSize=12;fontStyle=1;"
FLOW = ("endArrow=blockThin;endFill=1;html=1;rounded=1;edgeStyle=orthogonalEdgeStyle;strokeColor=#5A6B80;"
        "strokeWidth=1.5;fontSize=11;fontColor=#333333;labelBackgroundColor=#FFFFFF;")


def c4label(name, kind, desc=""):
    s = f"<b style='font-size:14px'>{escape(name)}</b><br><span style='font-size:10px'>[{escape(kind)}]</span>"
    if desc:
        s += f"<br><br><span style='font-size:11px'>{escape(desc)}</span>"
    return s


def rel(text, tech=""):
    s = escape(text)
    if tech:
        s += f"<br><span style='font-size:10px'>[{escape(tech)}]</span>"
    return s


class Page:
    def __init__(self, name):
        self.name = name
        self.cells = []
        self.n = 0

    def _id(self):
        self.n += 1
        return f"c{self.n}"

    def add(self, label, style, x, y, w, h, parent="1"):
        cid = self._id()
        self.cells.append(
            f'<mxCell id="{cid}" value="{escape(label, {chr(34): "&quot;"})}" style="{style}" vertex="1" parent="{parent}">'
            f'<mxGeometry x="{x}" y="{y}" width="{w}" height="{h}" as="geometry"/></mxCell>')
        return cid

    def link(self, src, dst, label="", style=REL, points=None):
        cid = self._id()
        pts = ""
        if points:
            pts = "<Array as=\"points\">" + "".join(f'<mxPoint x="{x}" y="{y}"/>' for x, y in points) + "</Array>"
        self.cells.append(
            f'<mxCell id="{cid}" value="{escape(label, {chr(34): "&quot;"})}" style="{style}" edge="1" parent="1" '
            f'source="{src}" target="{dst}"><mxGeometry relative="1" as="geometry">{pts}</mxGeometry></mxCell>')
        return cid

    def title(self, text, note=""):
        self.add(f"<b>{escape(text)}</b>", TITLE, 20, 10, 1200, 34)
        if note:
            self.add(escape(note), NOTE, 20, 44, 1300, 40)

    def xml(self, idx):
        body = '<mxCell id="0"/><mxCell id="1" parent="0"/>' + "".join(self.cells)
        return (f'<diagram id="p{idx}" name="{escape(self.name)}"><mxGraphModel dx="1400" dy="900" grid="1" gridSize="10" '
                f'guides="1" tooltips="1" connect="1" arrows="1" fold="1" page="1" pageScale="1" pageWidth="1600" '
                f'pageHeight="1200" math="0" shadow="0"><root>{body}</root></mxGraphModel></diagram>')


pages = []

# ---- C4 level 1: system context -----------------------------------------------

p = Page("C1 System context")
p.title("Level 1 · System context", "Who uses the calendar display and which outside systems it depends on.")
andrei = p.add(c4label("Andrei", "Person", "Owner. Looks at the display, taps it, sets it up from a phone, and pushes new code."),
               PERSON, 560, 90, 260, 130)
display = p.add(c4label("Calendar display", "Software system",
                        "CrowPanel 5\" touch screen that shows meetings from several Google accounts and Google Tasks, "
                        "and lets you dismiss meetings and tick tasks."), SYSTEM, 520, 380, 340, 160)
gcal = p.add(c4label("Google Calendar", "External system", "Meetings in every Google account."), EXTERNAL, 80, 690, 260, 120)
gtasks = p.add(c4label("Google Tasks", "External system", "To-do lists in every Google account."), EXTERNAL, 420, 820, 260, 120)
github = p.add(c4label("GitHub", "External system", "Code, automatic builds and firmware releases."), EXTERNAL, 1100, 380, 260, 140)
ntp = p.add(c4label("Internet time servers", "External system", "time.google.com, pool.ntp.org"), EXTERNAL, 780, 690, 260, 120)
other = p.add(c4label("Outlook / iCloud calendars", "External system", "Optional, subscribed in Google Calendar by ICS link."),
              EXTERNAL, 80, 920, 260, 120)
p.link(andrei, display, rel("Reads meetings and tasks, dismisses, ticks; sets up accounts from a phone", "touch screen, web page"))
p.link(andrei, github, rel("Pushes code to main", "git"))
p.link(display, gcal, rel("Reads meetings, hides dismissed ones", "HTTPS via Apps Script"))
p.link(display, gtasks, rel("Reads tasks, marks them done", "HTTPS via Apps Script"))
p.link(display, ntp, rel("Sets its clock", "NTP"))
p.link(display, github, rel("Downloads new firmware", "HTTPS"))
p.link(other, gcal, rel("Shown as calendars", "ICS"))
pages.append(p)

# ---- C4 level 2: containers ---------------------------------------------------

p = Page("C2 Containers")
p.title("Level 2 · Containers", "The parts of the calendar display system and how they talk to each other.")
p.add("<b>Calendar display</b> [Software system]", BOUNDARY, 40, 240, 1120, 760)
andrei = p.add(c4label("Andrei", "Person"), PERSON, 440, 90, 220, 90)
screen = p.add(c4label("Touch screen and speaker", "Hardware: 800x480 RGB LCD, GT911 touch, I2S amplifier",
                       "Shows the UI, reads taps, plays the chime."), CONTAINER, 80, 290, 280, 140)
setup = p.add(c4label("Setup web page", "HTML served by the firmware at http://calendar.local",
                      "Add or remove accounts, upload firmware, change the update link. Needs the PIN on screen."),
              CONTAINER, 420, 290, 300, 150)
fw = p.add(c4label("Firmware", "C++, Arduino, FreeRTOS, LVGL 8, LovyanGFX on the ESP32-S3",
                   "UI on core 1; Wi-Fi, sync, web page and updates on core 0."), CONTAINER, 380, 540, 340, 160)
nvs = p.add(c4label("Settings", "NVS flash (Preferences)", "Wi-Fi, accounts, display options as JSON."), STORE, 80, 800, 220, 130)
sd = p.add(c4label("microSD card", "FAT, JSON files", "Offline copy of each account's feed, settings backup."), STORE, 340, 800, 220, 130)
script = p.add(c4label("Apps Script web app", "Google Apps Script (one copy per Google account)",
                       "Returns meetings, tasks and calendars as JSON; dismisses, completes, shows or hides calendars."),
               CONTAINER, 800, 540, 340, 160)
ci = p.add(c4label("Build pipeline", "GitHub Actions, PlatformIO, esptool",
                   "Builds on every push; numbers the version; publishes a release from main."), CONTAINER, 880, 800, 260, 150)
rel_store = p.add(c4label("Firmware releases", "GitHub Releases", "firmware.bin, factory.bin, manifest.json"),
                  STORE, 610, 800, 230, 130)
gcal = p.add(c4label("Google Calendar", "External system"), EXTERNAL, 1260, 480, 220, 90)
gtasks = p.add(c4label("Google Tasks", "External system"), EXTERNAL, 1260, 640, 220, 90)
ntp = p.add(c4label("Time servers", "External system"), EXTERNAL, 1260, 300, 220, 90)
p.link(andrei, screen, rel("Reads and taps"))
p.link(andrei, setup, rel("Adds accounts, uploads firmware", "browser, HTTP"))
p.link(andrei, ci, rel("Pushes code", "git"), points=[(1200, 135), (1200, 875)])
p.link(screen, fw, rel("Taps", "I2C"))
p.link(fw, screen, rel("Draws frames, plays sound", "RGB bus, I2S"), points=[(220, 620)])
p.link(setup, fw, rel("Account and firmware requests", "HTTP form, PIN"))
p.link(fw, nvs, rel("Reads and saves settings"))
p.link(fw, sd, rel("Caches feeds, backs up settings", "SPI"))
p.link(fw, script, rel("Fetches feed, sends actions", "HTTPS GET + token, JSON"))
p.link(script, gcal, rel("Reads events", "CalendarApp"))
p.link(script, gtasks, rel("Reads and updates tasks", "Tasks API"))
p.link(ci, rel_store, rel("Publishes on push to main"))
p.link(fw, rel_store, rel("Checks manifest, downloads firmware", "HTTPS"))
p.link(fw, ntp, rel("Sets the clock", "NTP"))
pages.append(p)

# ---- C4 level 3: firmware components ------------------------------------------

p = Page("C3 Firmware components")
p.title("Level 3 · Components of the firmware",
        "Core 1 runs only the UI so taps never wait on the network. Core 0 runs the background tasks. "
        "They share data through the sync snapshot (copied under a lock) and the settings store.")
p.add("<b>Core 1 · UI task</b> (Arduino loop)", BOUNDARY, 30, 100, 640, 760)
p.add("<b>Core 0 · background tasks</b>", BOUNDARY, 720, 100, 620, 760)
main = p.add(c4label("Main loop", "main.cpp", "Starts every module; every loop: new snapshot to UI, UI tick, LVGL draw."),
             COMPONENT, 60, 130, 260, 110)
uicore = p.add(c4label("UI core", "ui.cpp", "Navigation, reminders, night mode, dimming, auto-update timing, toasts."),
               COMPONENT, 370, 130, 270, 120)
screens = p.add(c4label("Screens", "ui_home, ui_detail, ui_week, ui_tasks, ui_night, ui_wifi, ui_accounts, ui_settings",
                        "Built once; data parts rebuilt only when changed."), COMPONENT, 370, 300, 270, 140)
overlays = p.add(c4label("Overlays", "ui_overlays.cpp", "Meeting reminder, firmware update, theme picker, calendar chooser."),
                 COMPONENT, 370, 490, 270, 120)
theme = p.add(c4label("Themes", "theme.cpp", "Six colour sets."), COMPONENT, 60, 490, 240, 90)
disp = p.add(c4label("Display driver", "display.cpp, LovyanGFX", "RGB panel, touch, backlight, LVGL flush with DMA."),
             COMPONENT, 60, 300, 260, 120)
batt = p.add(c4label("Battery", "battery.cpp", "Optional MAX17048 fuel gauge."), COMPONENT, 60, 660, 240, 90)
sync = p.add(c4label("Sync worker", "sync.cpp", "Fetches every account, queues taps as actions, keeps the snapshot, offline cache."),
             COMPONENT, 760, 130, 270, 130)
wifi = p.add(c4label("Wi-Fi manager", "wifi_mgr.cpp", "Connect, reconnect, scan."), COMPONENT, 1060, 130, 250, 100)
web = p.add(c4label("Web server", "web.cpp", "Setup page, PIN check, firmware upload."), COMPONENT, 1060, 320, 250, 110)
ota = p.add(c4label("Updater", "ota.cpp", "Reads the manifest, installs to the other app slot, restarts."),
            COMPONENT, 760, 320, 270, 120)
audio = p.add(c4label("Audio", "audio.cpp", "Chime generator on I2S."), COMPONENT, 760, 500, 210, 90)
settings = p.add(c4label("Settings store", "settings.cpp", "Thread-safe settings with a version counter."),
                 COMPONENT, 760, 660, 270, 110)
storage = p.add(c4label("SD storage", "storage.cpp", "Atomic file writes."), COMPONENT, 1060, 660, 250, 100)
script = p.add(c4label("Apps Script web app", "Container"), CONTAINER, 1420, 130, 200, 90)
rels = p.add(c4label("Firmware releases", "Container"), STORE, 1420, 600, 200, 110)
lcd = p.add(c4label("Touch screen", "Hardware"), CONTAINER, 60, 920, 200, 80)
nvs = p.add(c4label("NVS flash", "Container"), STORE, 760, 920, 200, 100)
sdc = p.add(c4label("microSD card", "Container"), STORE, 1060, 920, 200, 100)
browser = p.add(c4label("Browser", "Phone or PC"), EXTERNAL, 1420, 335, 200, 80)
p.link(main, uicore, rel("ui_update, ui_tick"))
p.link(main, disp, rel("lv_timer_handler"))
p.link(uicore, screens, rel("show, refresh"))
p.link(uicore, overlays, rel("reminder, update dialog"), points=[(330, 560)])
p.link(screens, theme, rel("colours"))
p.link(disp, lcd, rel("pixels, touch"), points=[(45, 360), (45, 960)])
p.link(screens, sync, rel("dismiss, complete, undo, week, calendars"))
p.link(sync, main, rel("snapshot (sync_get_if_newer)"), points=[(700, 110), (190, 110)])
p.link(sync, wifi, rel("wifi_service"))
p.link(sync, script, rel("HTTPS + token"))
p.link(sync, storage, rel("feed cache"), points=[(1045, 280), (1045, 640)])
p.link(web, settings, rel("add or remove accounts"))
p.link(web, ota, rel("upload chunks"))
p.link(browser, web, rel("HTTP"))
p.link(overlays, ota, rel("check, install"))
p.link(ota, rels, rel("manifest, firmware.bin"))
p.link(uicore, audio, rel("chime"), points=[(700, 545)])
p.link(screens, batt, rel("level"), points=[(340, 700)])
p.link(settings, nvs, rel("save"))
p.link(settings, storage, rel("backup"))
p.link(storage, sdc, rel("SPI"))
p.link(uicore, settings, rel("read, save"), points=[(690, 230), (690, 715)])
pages.append(p)

# ---- C4 level 3: Apps Script components ---------------------------------------

p = Page("C3 Apps Script components")
p.title("Level 3 · Components of the Apps Script web app (apps-script/Code.gs)",
        "One copy runs in each Google account, as that account. Every call is GET <url>?token=T&action=...")
fw = p.add(c4label("Firmware sync worker", "Container"), CONTAINER, 40, 330, 220, 100)
router = p.add(c4label("doGet", "Router", "Checks the token, picks the action, returns JSON."), COMPONENT, 330, 320, 240, 120)
feed = p.add(c4label("feed_", "Action: none", "Account, time zone, calendars, next 7 days of events, open tasks."),
             COMPONENT, 660, 100, 260, 110)
rng = p.add(c4label("range_", "Action: range", "Events between two times (week view)."), COMPONENT, 660, 240, 260, 100)
status = p.add(c4label("setTaskStatus_", "Action: complete / uncomplete", "Marks a task done or open again."),
               COMPONENT, 660, 370, 260, 100)
dismiss = p.add(c4label("dismissEvent_", "Action: dismiss", "Remembers a dismissed meeting until it ends."),
                COMPONENT, 660, 500, 260, 100)
showcal = p.add(c4label("showCalendar_", "Action: showcal", "Shows or hides one calendar on the display."),
                COMPONENT, 660, 630, 260, 100)
events = p.add(c4label("getEvents_", "Helper", "Reads shown calendars, skips dismissed, trims text."),
               COMPONENT, 1010, 170, 250, 110)
tasks = p.add(c4label("getTasks_", "Helper", "Reads task lists (CONFIG.TASKLIST_IDS or all)."), COMPONENT, 1010, 370, 250, 100)
props = p.add(c4label("Script properties", "Store", "dismissed, hiddenCalendars (guarded by withLock_)"),
              STORE, 1010, 540, 250, 130)
gcal = p.add(c4label("Google Calendar", "External system"), EXTERNAL, 1340, 170, 220, 90)
gtasks = p.add(c4label("Google Tasks", "External system"), EXTERNAL, 1340, 370, 220, 90)
p.add("<b>CONFIG</b> at the top of Code.gs: TOKEN, DAYS_AHEAD, MAX_EVENTS, MAX_RANGE_EVENTS, MAX_TASKS, TASKLIST_IDS",
      NOTE, 40, 780, 900, 30)
p.link(fw, router, rel("GET ?token&action", "HTTPS"))
for comp in (feed, rng, status, dismiss, showcal):
    p.link(router, comp)
p.link(feed, events)
p.link(rng, events)
p.link(feed, tasks)
p.link(status, gtasks, rel("Tasks.Tasks.patch"), points=[(1300, 420)])
p.link(events, gcal, rel("CalendarApp"))
p.link(tasks, gtasks, rel("Tasks.Tasks.list"))
p.link(dismiss, props)
p.link(showcal, props)
p.link(events, props, rel("reads"))
pages.append(p)

# ---- Flows --------------------------------------------------------------------

LANE_W = 270
ROW_H = 96
TOP = 110


def flow(name, intro, lanes, steps, links):
    """lanes: names left to right. steps: (key, lane, row, text, kind). links: (from, to, label)."""
    pg = Page("Flow · " + name)
    pg.title("Flow · " + name, intro)
    rows = max(s[2] for s in steps) + 1
    height = 40 + rows * ROW_H + 20
    # A lane holds one column of steps; "Lane|R" puts a step in a second column
    # on the right of that lane (for the other branch of a decision).
    wide = {st[1].split("|")[0] for st in steps if st[1].endswith("|R")}
    lane_x, x = {}, 20
    for lane in lanes:
        w = LANE_W * 2 if lane in wide else LANE_W
        lane_x[lane] = x
        pg.add(escape(lane), LANE, x, TOP, w, height)
        x += w
    taken = {}
    for key, lane, row, _, _ in steps:
        if (lane, row) in taken:
            print(f"OVERLAP {name}: {key} and {taken[(lane, row)]} share {lane} row {row}")
        taken[(lane, row)] = key
    ids = {}
    for key, lane, row, text, kind in steps:
        base, right = lane.split("|")[0], lane.endswith("|R")
        cx = lane_x[base] + LANE_W / 2 + (LANE_W if right else 0)
        cy = TOP + 40 + row * ROW_H + ROW_H / 2
        if kind == "start":
            w, h, st = 150, 56, START
        elif kind == "end":
            w, h, st = 150, 56, END
        elif kind == "decide":
            w, h, st = 200, 84, DECIDE
        else:
            w, h, st = 230, 66, STEP
        ids[key] = pg.add(escape(text), st, cx - w / 2, cy - h / 2, w, h)
    for a, b, label in links:
        pg.link(ids[a], ids[b], escape(label), FLOW)
    pages.append(pg)


UI = "Person"
SCREEN = "Display UI (core 1)"
SYNC = "Sync worker (core 0)"
SCRIPT = "Apps Script"
GOOGLE = "Google"
WEB = "Web server (core 0)"
OTA = "Updater (core 0)"

flow("Start-up",
     "What happens from power-on until the first meetings are on screen.",
     ["Firmware (setup)", SCREEN, SYNC, "Wi-Fi manager"],
     [("on", "Firmware (setup)", 0, "Power on", "start"),
      ("st", "Firmware (setup)", 1, "storage_begin: mount SD card", "step"),
      ("se", "Firmware (setup)", 2, "settings_begin: load from NVS (or SD backup)", "step"),
      ("hw", "Firmware (setup)", 3, "display, battery, audio, Wi-Fi start", "step"),
      ("ui", SCREEN, 4, "ui_init: build every screen once in the saved theme", "step"),
      ("first", SCREEN, 5, "Wi-Fi set up?", "decide"),
      ("wifi", SCREEN, 6, "Open the Wi-Fi screen and scan", "step"),
      ("home", SCREEN, 7, "Open Home", "step"),
      ("ss", "Firmware (setup)", 5, "sync_start, web_begin, ota_begin (tasks on core 0)", "step"),
      ("cache", SYNC, 6, "Load the offline copy from SD and publish it", "step"),
      ("conn", "Wi-Fi manager", 7, "Connect with saved network", "step"),
      ("ntp", SYNC, 8, "Set the clock from NTP", "step"),
      ("fetch", SYNC, 9, "Fetch every account (see Background sync)", "step"),
      ("show", SCREEN, 10, "New snapshot: refresh the visible screen", "end")],
     [("on", "st", ""), ("st", "se", ""), ("se", "hw", ""), ("hw", "ui", ""), ("ui", "first", ""),
      ("first", "wifi", "no"), ("first", "home", "yes"), ("ui", "ss", ""), ("ss", "cache", ""),
      ("cache", "conn", ""), ("conn", "ntp", "connected"), ("ntp", "fetch", ""), ("fetch", "show", "")])

flow("Connect to Wi-Fi",
     "First start, or Settings > Change network. The password is typed on the on-screen keyboard.",
     [UI, SCREEN, "Wi-Fi manager", "Settings store"],
     [("open", UI, 0, "Opens the Wi-Fi screen", "start"),
      ("scan", SCREEN, 1, "wifi_scan_start; show 'Scanning...'", "step"),
      ("list", "Wi-Fi manager", 2, "Scan; return networks sorted by signal", "step"),
      ("pick", UI, 3, "Taps a network", "step"),
      ("locked", SCREEN, 4, "Needs a password?", "decide"),
      ("type", UI, 5, "Types the password, taps the tick", "step"),
      ("req", SCREEN, 6, "wifi_connect(ssid, password)", "step"),
      ("try", "Wi-Fi manager", 7, "Connected within 15 s?", "decide"),
      ("save", "Settings store", 8, "Save network and password", "step"),
      ("ok", SCREEN, 9, "Show 'Connected', go to Settings", "end"),
      ("fail", SCREEN, 8, "Show 'Could not connect'; reconnect to the old network", "end")],
     [("open", "scan", ""), ("scan", "list", ""), ("list", "pick", ""), ("pick", "locked", ""),
      ("locked", "type", "yes"), ("locked", "req", "no (Connect)"), ("type", "req", ""), ("req", "try", ""),
      ("try", "save", "yes"), ("save", "ok", ""), ("try", "fail", "no")])

flow("Add a Google account",
     "Done once per account. The setup page is opened from a phone by scanning the QR code on the Accounts screen.",
     [UI, "Google (Apps Script editor)", WEB, "Settings store", SYNC],
     [("deploy", UI, 0, "Pastes Code.gs, sets TOKEN, deploys as web app", "start"),
      ("url", "Google (Apps Script editor)", 1, "Gives a web app URL ending in /exec", "step"),
      ("scan", UI, 2, "Scans the QR code, opens the setup page", "step"),
      ("form", UI, 3, "Enters name, URL, token and the PIN from the display", "step"),
      ("pin", WEB, 4, "PIN right? (5 wrong = 1 min lock)", "decide"),
      ("valid", WEB, 5, "URL is script.google.com/.../exec and fewer than 6 accounts?", "decide"),
      ("save", "Settings store", 6, "Add account with the next colour; save to NVS and SD", "step"),
      ("refresh", SYNC, 7, "Settings changed: reset data, fetch all accounts", "step"),
      ("done", UI, 8, "Page says 'Account added'; meetings appear", "end"),
      ("err", WEB, 6, "Show the error", "end")],
     [("deploy", "url", ""), ("url", "scan", ""), ("scan", "form", ""), ("form", "pin", ""), ("pin", "valid", "yes"),
      ("pin", "err", "no"), ("valid", "save", "yes"), ("valid", "err", "no"), ("save", "refresh", ""),
      ("refresh", "done", "")])

flow("Background sync",
     "The sync worker loop on core 0. It wakes every second (5 s offline) or straight away after a tap.",
     [SYNC, SCRIPT, GOOGLE, "SD card", SCREEN],
     [("wake", SYNC, 0, "Wake up", "start"),
      ("up", SYNC, 1, "Wi-Fi up?", "decide"),
      ("offline", SCREEN, 2, "Show 'No Wi-Fi connection'; keep the last data", "end"),
      ("act", SYNC, 2, "Send queued taps (see Dismiss / Complete)", "step"),
      ("due", SYNC, 3, "Refresh due (every N min), a tap, or Sync now?", "decide"),
      ("get", SYNC, 4, "For each account: GET url?token=...", "step"),
      ("feed", SCRIPT, 4, "feed_: calendars, events (7 days), open tasks", "step"),
      ("read", GOOGLE, 4, "Calendar and Tasks", "step"),
      ("ok", SYNC, 5, "Reply OK?", "decide"),
      ("cache", "SD card", 6, "Save the reply as the offline copy", "step"),
      ("keep", SYNC + "|R", 6, "Keep the old data for that account; note the error", "step"),
      ("merge", SYNC, 7, "Merge accounts, drop confirmed hidden items, sort, set time zone", "step"),
      ("pub", SYNC, 8, "Publish new snapshot (version + 1)", "step"),
      ("draw", SCREEN, 9, "Main loop picks it up; visible screen refreshes", "end"),
      ("sleep", SYNC + "|R", 3, "Sleep until next wake", "end")],
     [("wake", "up", ""), ("up", "offline", "no"), ("up", "act", "yes"), ("act", "due", ""), ("due", "get", "yes"),
      ("due", "sleep", "no"), ("get", "feed", ""), ("feed", "read", ""), ("read", "ok", "JSON"), ("ok", "cache", "yes"),
      ("ok", "keep", "no"), ("cache", "merge", ""), ("keep", "merge", ""), ("merge", "pub", ""), ("pub", "draw", "")])

flow("Dismiss a meeting",
     "Dismiss only hides a meeting on the display; Google Calendar is not changed. The screen updates at once.",
     [UI, SCREEN, SYNC, SCRIPT],
     [("tap", UI, 0, "Taps X on a meeting (Home or details)", "start"),
      ("local", SYNC, 1, "Remove it from the snapshot, mark hidden, queue 'dismiss'", "step"),
      ("redraw", SCREEN, 2, "Row disappears; toast 'Hidden from the display'", "step"),
      ("send", SYNC, 3, "GET ?action=dismiss&event=KEY&until=END", "step"),
      ("store", SCRIPT, 4, "Store KEY in 'dismissed' until the meeting ends", "step"),
      ("ok", SYNC, 5, "Reply OK?", "decide"),
      ("confirm", SYNC, 6, "Keep hidden; next sync no longer returns it", "end"),
      ("back", SYNC + "|R", 6, "Un-hide; it comes back on the next sync; show the error", "end")],
     [("tap", "local", ""), ("local", "redraw", ""), ("local", "send", "worker wakes"), ("send", "store", ""),
      ("store", "ok", ""), ("ok", "confirm", "yes"), ("ok", "back", "no")])

flow("Complete a task (with Undo)",
     "Same on Home and Tasks. The tick shows for 0.45 s, then the task leaves the list and Undo is offered for 7 s.",
     [UI, SCREEN, SYNC, SCRIPT],
     [("tap", UI, 0, "Taps the circle next to a task", "start"),
      ("tick", SCREEN, 1, "Show the tick (450 ms)", "step"),
      ("local", SYNC, 2, "Remove the task, mark hidden, queue 'complete'", "step"),
      ("toast", SCREEN, 3, "Toast 'Done: <task>' with Undo", "step"),
      ("undo", UI, 4, "Taps Undo?", "decide"),
      ("sent", SYNC, 5, "'complete' already sent?", "decide"),
      ("cancel", SYNC, 6, "Drop the queued action; put the task back", "end"),
      ("unc", SYNC, 7, "Queue 'uncomplete'; put the task back", "step"),
      ("send", SYNC + "|R", 5, "GET ?action=complete&list=L&task=ID", "step"),
      ("patch", SCRIPT, 6, "Tasks: status = completed (or needsAction for undo)", "step"),
      ("end", SYNC, 8, "Next sync confirms the state", "end")],
     [("tap", "tick", ""), ("tick", "local", ""), ("local", "toast", ""), ("toast", "undo", ""), ("undo", "sent", "yes"),
      ("undo", "send", "no"), ("sent", "cancel", "no"), ("sent", "unc", "yes"), ("unc", "patch", ""),
      ("send", "patch", ""), ("patch", "end", "")])

flow("Week view",
     "The week is loaded on demand for the week you look at.",
     [UI, SCREEN, SYNC, SCRIPT],
     [("open", UI, 0, "Opens Week, or taps < / This week / >", "start"),
      ("req", SCREEN, 1, "sync_request_week(Monday 00:00)", "step"),
      ("load", SCREEN, 2, "Show 'Loading the week...'", "step"),
      ("get", SYNC, 3, "For each account: GET ?action=range&from&to", "step"),
      ("rng", SCRIPT, 4, "range_: events in those 7 days", "step"),
      ("pub", SYNC, 5, "Publish the week in the snapshot", "step"),
      ("draw", SCREEN, 6, "Draw 7 day columns; today outlined", "end")],
     [("open", "req", ""), ("req", "load", ""), ("req", "get", ""), ("get", "rng", ""), ("rng", "pub", ""),
      ("pub", "draw", "")])

flow("Meeting reminder",
     "Checked by the UI every 200 ms. Each meeting rings once unless snoozed.",
     [SCREEN, "Audio", UI],
     [("tick", SCREEN, 0, "ui_tick", "start"),
      ("due", SCREEN, 1, "Meeting starts within the reminder time (default 5 min)?", "decide"),
      ("on", SCREEN, 2, "Chime on, or turned on for this meeting, and not already shown?", "decide"),
      ("wake", SCREEN, 3, "Leave night clock, full brightness", "step"),
      ("show", SCREEN, 4, "Show reminder: title, time, place", "step"),
      ("chime", "Audio", 4, "Play two-note chime at the set volume", "step"),
      ("choice", UI, 5, "Snooze 5 min / Details / Got it", "decide"),
      ("snooze", SCREEN, 6, "Ring again in 5 minutes", "end"),
      ("det", SCREEN, 7, "Open meeting details", "end"),
      ("ok", SCREEN, 8, "Close", "end"),
      ("skip", SCREEN + "|R", 2, "Nothing to do", "end")],
     [("tick", "due", ""), ("due", "on", "yes"), ("due", "skip", "no"), ("on", "wake", "yes"), ("on", "skip", "no"),
      ("wake", "show", ""), ("show", "chime", ""), ("show", "choice", ""), ("choice", "snooze", "Snooze"),
      ("choice", "det", "Details"), ("choice", "ok", "Got it")])

flow("Night mode and dimming",
     "Night window and dim delay are set in Settings > Display and sound.",
     [SCREEN, UI],
     [("tick", SCREEN, 0, "ui_tick", "start"),
      ("night", SCREEN, 1, "Inside the night window?", "decide"),
      ("idle", SCREEN, 2, "No touch for 30 s and no reminder?", "decide"),
      ("clock", SCREEN, 3, "Show night clock, backlight 6%", "step"),
      ("tap", UI, 4, "Taps the screen", "step"),
      ("home", SCREEN, 5, "Back to the screen before, full brightness", "end"),
      ("day", SCREEN + "|R", 2, "Night clock showing? Back to Home", "step"),
      ("dim", SCREEN + "|R", 3, "Dim on and idle for N min? Backlight 20%", "step"),
      ("touch", SCREEN + "|R", 4, "Touched again: full brightness", "end")],
     [("tick", "night", ""), ("night", "idle", "yes"), ("idle", "clock", "yes"), ("clock", "tap", ""), ("tap", "home", ""),
      ("night", "day", "no"), ("day", "dim", ""), ("dim", "touch", "")])

flow("Change theme",
     "Six themes: Midnight, Ocean, Forest, Sunset, Paper, High contrast.",
     [UI, SCREEN, "Settings store"],
     [("open", UI, 0, "Settings > Theme", "start"),
      ("pick", SCREEN, 1, "Show six previews, current one ticked", "step"),
      ("tap", UI, 2, "Taps a theme", "step"),
      ("save", "Settings store", 3, "Save theme index (NVS + SD)", "step"),
      ("build", SCREEN, 4, "Rebuild every screen in the new colours", "step"),
      ("again", SCREEN, 5, "Re-open the picker so you can compare", "end")],
     [("open", "pick", ""), ("pick", "tap", ""), ("tap", "save", ""), ("save", "build", ""), ("build", "again", "")])

flow("Choose calendars",
     "Per account: which of its calendars appear on the display.",
     [UI, SCREEN, SYNC, SCRIPT],
     [("open", UI, 0, "Accounts > Choose calendars", "start"),
      ("list", SCREEN, 1, "List the account's calendars with switches", "step"),
      ("flip", UI, 2, "Flips a switch", "step"),
      ("queue", SYNC, 3, "Queue 'showcal'", "step"),
      ("store", SCRIPT, 4, "showCalendar_: update hiddenCalendars", "step"),
      ("done", UI, 5, "Taps Done", "step"),
      ("sync", SYNC, 6, "Sync now: feed without the hidden calendars", "end")],
     [("open", "list", ""), ("list", "flip", ""), ("flip", "queue", ""), ("queue", "store", ""), ("store", "done", ""),
      ("done", "sync", "")])

flow("Save settings",
     "Every change in Settings (sliders on release, switches, theme, 24/12 h) goes the same way.",
     [SCREEN, "Settings store", "NVS flash", "SD card", "Other modules"],
     [("chg", SCREEN, 0, "User changes a setting", "start"),
      ("save", "Settings store", 1, "settings_save: copy under lock, version + 1", "step"),
      ("nvs", "NVS flash", 2, "Write JSON to Preferences", "step"),
      ("sd", "SD card", 3, "Write settings.json backup (atomic)", "step"),
      ("see", "Other modules", 4, "Sync worker, UI and updater notice the new version", "end")],
     [("chg", "save", ""), ("save", "nvs", ""), ("nvs", "sd", ""), ("sd", "see", "")])

flow("Remove an account",
     "From the display (Accounts > bin icon) or from the setup page with the PIN.",
     [UI, SCREEN, "Settings store", SYNC],
     [("tap", UI, 0, "Taps the bin icon", "start"),
      ("ask", SCREEN, 1, "Ask 'Remove <name>?'", "decide"),
      ("save", "Settings store", 2, "Remove the account; save", "step"),
      ("reset", SYNC, 3, "Accounts changed: clear data and pending taps, fetch again", "step"),
      ("done", SCREEN, 4, "Its meetings and tasks leave the display", "end"),
      ("keep", SCREEN, 2, "Nothing changes", "end")],
     [("tap", "ask", ""), ("ask", "save", "Remove"), ("ask", "keep", "Cancel"), ("save", "reset", ""),
      ("reset", "done", "")])

flow("Update firmware from the setup page",
     "Works without GitHub. The new firmware is written to the other app slot; the running one is untouched until restart.",
     [UI, WEB, OTA, SCREEN],
     [("pick", UI, 0, "Chooses firmware.bin, enters the PIN, taps Upload", "start"),
      ("pin", WEB, 1, "PIN right?", "decide"),
      ("start", OTA, 2, "Update.begin: erase the other slot", "step"),
      ("dlg", SCREEN, 3, "Update dialog opens by itself with progress", "step"),
      ("write", OTA, 3, "Write each received chunk", "step"),
      ("end", OTA, 4, "Image complete and valid?", "decide"),
      ("boot", OTA, 5, "Mark new slot bootable; restart after 2.5 s", "end"),
      ("fail", SCREEN, 5, "Show the error; old firmware keeps running", "end"),
      ("deny", WEB, 2, "403 Wrong PIN", "end")],
     [("pick", "pin", ""), ("pin", "start", "yes"), ("pin", "deny", "no"), ("start", "dlg", ""), ("start", "write", ""),
      ("write", "end", ""), ("end", "boot", "yes"), ("end", "fail", "no")])

flow("Automatic update after a push to main",
     "Every push to main becomes a release; each display installs it the next time nobody is using it.",
     [UI, "GitHub Actions", "GitHub Releases", SCREEN, OTA],
     [("push", UI, 0, "Pushes to main", "start"),
      ("build", "GitHub Actions", 1, "Build with version 1.0.<run number>", "step"),
      ("pack", "GitHub Actions", 2, "Make factory.bin, firmware.bin, manifest.json", "step"),
      ("rel", "GitHub Releases", 3, "Publish release v1.0.N as latest", "step"),
      ("timer", SCREEN, 4, "Every 3 h (first after 1 min): auto-update on?", "decide"),
      ("check", OTA, 5, "Read releases/latest/download/manifest.json", "step"),
      ("newer", OTA, 6, "Version newer than the running one?", "decide"),
      ("quiet", SCREEN, 7, "Night window or 15 min without touch, and no meeting in 15 min?", "decide"),
      ("inst", OTA, 8, "Download firmware.bin into the other slot", "step"),
      ("restart", OTA, 9, "Restart into the new version; settings kept", "end"),
      ("wait", SCREEN, 8, "Try again on the next tick", "end"),
      ("same", OTA, 7, "Up to date", "end")],
     [("push", "build", ""), ("build", "pack", ""), ("pack", "rel", ""), ("rel", "timer", ""), ("timer", "check", "yes"),
      ("check", "newer", ""), ("newer", "quiet", "yes"), ("newer", "same", "no"), ("quiet", "inst", "yes"),
      ("quiet", "wait", "no"), ("inst", "restart", "")])

flow("Check for update by hand",
     "Settings > Check for update.",
     [UI, SCREEN, OTA],
     [("tap", UI, 0, "Taps Check for update", "start"),
      ("dlg", SCREEN, 1, "Open update dialog: 'Checking...'", "step"),
      ("check", OTA, 2, "Read the manifest at the update link", "step"),
      ("res", OTA, 3, "Result?", "decide"),
      ("latest", SCREEN, 4, "'You have the latest version'", "end"),
      ("avail", SCREEN, 5, "'Version X is available' + Install", "step"),
      ("inst", UI, 6, "Taps Install", "step"),
      ("go", OTA, 7, "Install as in the automatic flow, then restart", "end"),
      ("err", SCREEN + "|R", 4, "Error + how to upload from the setup page", "end")],
     [("tap", "dlg", ""), ("dlg", "check", ""), ("check", "res", ""), ("res", "latest", "same"), ("res", "avail", "newer"),
      ("res", "err", "failed"), ("avail", "inst", ""), ("inst", "go", "")])

flow("First flash over USB",
     "Done once. After this every update arrives over Wi-Fi.",
     [UI, "Browser (esptool-js)", "Board"],
     [("dl", UI, 0, "Downloads factory.bin from a release or Actions run", "start"),
      ("plug", UI, 1, "Connects the board by USB-C", "step"),
      ("conn", "Browser (esptool-js)", 2, "Connect; board found?", "decide"),
      ("boot", UI, 3, "Hold BOOT, tap RESET, release BOOT", "step"),
      ("flash", "Browser (esptool-js)", 4, "Write factory.bin at 0x0", "step"),
      ("reset", "Board", 5, "Press RESET; firmware starts (see Start-up)", "end")],
     [("dl", "plug", ""), ("plug", "conn", ""), ("conn", "flash", "yes"), ("conn", "boot", "no"), ("boot", "conn", ""),
      ("flash", "reset", "")])

# ---- Write --------------------------------------------------------------------

xml = ('<mxfile host="build_drawio.py" type="device">' + "".join(pg.xml(i) for i, pg in enumerate(pages)) + "</mxfile>\n")
with open(OUT, "w", encoding="utf-8") as f:
    f.write(xml)
print(f"wrote {os.path.normpath(OUT)}: {len(pages)} pages")
