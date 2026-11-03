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

if mapfile and os.path.exists(mapfile):
    per_lib = collections.Counter()
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
            elif "/src/" in obj.replace("\\", "/"):
                lib = "app: " + os.path.basename(obj)
            elif "lvgl" in obj:
                lib = "lvgl (objects)"
            else:
                lib = os.path.basename(obj)
            per_lib[lib] += int(size, 16)
    print("== Bytes per library / app file (flash sections) ==")
    total = sum(per_lib.values())
    for lib, n in per_lib.most_common(70):
        print("%9d  %5.1f%%  %s" % (n, 100.0 * n / max(total, 1), lib))
    print("%9d  total" % total)
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
