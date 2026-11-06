#!/usr/bin/env python3
# Where the flash goes: totals per library (from the linker map, if there is
# one) and the biggest symbols (from the ELF). Used by CI; the report lands in
# dist/size-<board>.txt on the builds/ branch.
#   size_report.py <nm> <firmware.elf> [firmware.map]
import collections
import os
import re
import subprocess
import sys

nm, elf = sys.argv[1], sys.argv[2]
mapfile = sys.argv[3] if len(sys.argv) > 3 else ""

# Sections that end up in the flash image (code, constants, initialised data).
FLASH = re.compile(r"^\.(flash\.(text|rodata|appdesc)|iram0\.text|dram0\.data|rodata|text|data)")

AREAS = [  # first match wins
    ("text constants (messages, log and error texts, UI words)", lambda o, s: ".str1." in s),
    ("Bluetooth (NimBLE, controller, presence)", lambda o, s: re.search(r"NimBLE|libbt|presence\.cpp", o)),
    ("Wi-Fi (driver, radio, WPA)", lambda o, s: re.search(r"lib(net80211|pp|phy|wpa_supplicant|esp_wifi|coexist|mesh|core|espnow|smartconfig)\.a", o)),
    ("TLS and certificates", lambda o, s: re.search(r"libmbed|crt_bundle|esp-tls|NetworkClientSecure|ssl_client", o)),
    ("TCP/IP, HTTP, mDNS, web server", lambda o, s: re.search(r"liblwip|mdns|HTTP|WebServer|Parsing|Network|libesp_netif|libesp_http", o)),
    ("fonts", lambda o, s: "/fonts/" in o),
    ("LVGL (UI library)", lambda o, s: "lvgl" in o),
    ("display driver (LovyanGFX, LCD)", lambda o, s: re.search(r"LovyanGFX|lgfx|libesp_lcd", o)),
    ("this firmware: screens (ui_*)", lambda o, s: "/src/ui" in o and "libdeps" not in o),
    ("this firmware: the rest", lambda o, s: "/src/" in o and "libdeps" not in o),
    ("C library (printf, scanf, maths)", lambda o, s: re.search(r"lib(c|m|gcc|stdc\+\+|newlib)\.a", o)),
]


def area(obj, sec):
    o = obj.replace("\\", "/")
    for name, test in AREAS:
        if test(o, sec):
            return name
    return "ESP-IDF and Arduino core (system, drivers)"


if mapfile and os.path.exists(mapfile):
    per_lib = collections.Counter()
    parts = collections.defaultdict(collections.Counter)  # lib -> input section -> bytes
    where = {}  # lib -> one full object path
    groups = collections.Counter()  # rough areas, for the summary
    section = ""
    pending = None
    line_re = re.compile(r"^\s*(\.\S+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$")
    with open(mapfile, errors="replace") as f:
        for raw in f:
            if raw.startswith("."):
                section = raw.split()[0]
            m = line_re.match(raw.rstrip())
            if pending and m and not m.group(1):
                m = line_re.match(pending + raw.rstrip())
            pending = None
            if not m:
                s = raw.strip()
                if s.startswith(".") and len(s.split()) == 1:
                    pending = " " + s
                continue
            sub, addr, size, obj = m.groups()
            if not FLASH.match(sub or section) or int(addr, 16) == 0:
                continue
            lib = obj
            mm = re.search(r"([^/\\]+\.a)\(", obj)
            if mm:
                lib = mm.group(1)
            elif "/src/" in obj.replace("\\", "/") and "libdeps" not in obj:
                lib = "app: " + os.path.basename(obj)
            elif "lvgl" in obj:
                lib = "lvgl (objects)"
            else:
                lib = os.path.basename(obj)
            per_lib[lib] += int(size, 16)
            groups[area(obj, sub or section)] += int(size, 16)
            parts[lib][sub or section] += int(size, 16)
            where.setdefault(lib, obj)
    print("== Bytes per library / app file (flash sections) ==")
    total = sum(per_lib.values())
    for lib, n in per_lib.most_common(70):
        print("%9d  %5.1f%%  %s" % (n, 100.0 * n / max(total, 1), lib))
    print("%9d  total" % total)
    print()
    print("== By area ==")
    for name, n in groups.most_common():
        print("%9d  %5.1f%%  %s" % (n, 100.0 * n / max(total, 1), name))
    print()
    print("== Biggest parts of the top 25 (input sections) ==")
    for lib, n in per_lib.most_common(25):
        print("%s  (%s)" % (lib, where[lib][-110:]))
        for sec, b in parts[lib].most_common(8):
            print("   %8d  %s" % (b, sec[:110]))
    print()
else:
    print("(no linker map)\n")

out = subprocess.run([nm, "-S", "--size-sort", "-C", "--radix=d", elf], capture_output=True, text=True).stdout
rows = []
for line in out.splitlines():
    parts = line.split(None, 3)
    if len(parts) == 4 and parts[2].lower() in "trdgb" and parts[2].lower() not in "b":
        rows.append((int(parts[1]), parts[2], parts[3]))
rows.sort(reverse=True)
print("== Biggest symbols (text/rodata/data) ==")
for size, kind, name in rows[:120]:
    print("%9d  %s  %s" % (size, kind, name[:140]))
