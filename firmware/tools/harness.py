#!/usr/bin/env python3
"""PC side of the display's test harness (src/harness.cpp).

  python tools/harness.py --port COM7 cmd "tap 400 240"
  python tools/harness.py --port COM7 shot home.png
  python tools/harness.py --http 192.168.1.50 --pin 123456 shot home.png
  python tools/harness.py --http 192.168.1.50 --pin 123456 cmd "screen week"
  python tools/harness.py decode --env crowpanel_7inch "Backtrace: 0x4200..:0x3fc9.. ..."

Needs pyserial and Pillow. Opening the port keeps the reset lines released,
so the display does not restart.
"""
import argparse
import base64
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import urllib.parse
import urllib.request

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE = os.path.dirname(HERE)


def open_port(port):
    import serial

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = s.rts = False
    s.open()
    s.reset_input_buffer()
    return s


def serial_cmd(port, line, timeout=10):
    s = open_port(port)
    s.write((line + "\n").encode())
    deadline, buf = time.time() + timeout, b""
    while time.time() < deadline:
        buf += s.read(4096)
        if b"H> " in buf and buf.endswith(b"\n"):
            break
    s.close()
    text = buf.decode("utf-8", "replace")
    return text[text.find("H> ") + 3:].strip() if "H> " in text else text.strip()


def rgb565_image(pixels, w, h):
    rgb = bytearray(w * h * 3)
    for i, p in enumerate(pixels):
        rgb[i * 3] = (p >> 11 & 31) * 255 // 31
        rgb[i * 3 + 1] = (p >> 5 & 63) * 255 // 63
        rgb[i * 3 + 2] = (p & 31) * 255 // 31
    return Image.frombytes("RGB", (w, h), bytes(rgb))


def serial_shot(port, out, timeout=120):
    s = open_port(port)
    s.write(b"shot\n")
    deadline, buf = time.time() + timeout, b""
    while time.time() < deadline and b"SHOT END" not in buf:
        buf += s.read(65536)
    s.close()
    text = buf.decode("ascii", "replace")
    m = re.search(r"SHOT (\d+) (\d+) (\d+)\r?\n(.*?)SHOT END", text, re.S)
    if not m:
        sys.exit("no screenshot in reply:\n" + text[-500:])
    w, h, size = int(m[1]), int(m[2]), int(m[3])
    rle = base64.b64decode("".join(m[4].split()))
    if len(rle) != size:
        sys.exit("screenshot cut short: %d of %d bytes" % (len(rle), size))
    pixels = []
    for count, colour in struct.iter_unpack("<HH", rle):
        pixels.extend([colour] * count)
    rgb565_image(pixels[: w * h], w, h).save(out)


def http_get(host, path, **params):
    url = "http://%s%s?%s" % (host, path, urllib.parse.urlencode(params))
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read()


def http_shot(host, pin, out):
    bmp = http_get(host, "/shot.bmp", pin=pin)
    tmp = out + ".bmp"
    with open(tmp, "wb") as f:
        f.write(bmp)
    Image.open(tmp).convert("RGB").save(out)
    os.remove(tmp)


def decode(env, text):
    elf = os.path.join(FIRMWARE, ".pio", "build", env, "firmware.elf")
    tool = shutil.which("xtensa-esp32s3-elf-addr2line") or os.path.join(
        os.path.expanduser("~"), ".platformio", "packages", "toolchain-xtensa-esp-elf", "bin",
        "xtensa-esp32s3-elf-addr2line")
    addrs = re.findall(r"0x4[0-9a-fA-F]{7}", text)
    if not addrs:
        sys.exit("no code addresses found")
    subprocess.run([tool, "-pfiaC", "-e", elf] + addrs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port, e.g. COM7")
    ap.add_argument("--http", help="display IP address (uses the setup page)")
    ap.add_argument("--pin", help="setup page PIN (Accounts screen)")
    sub = ap.add_subparsers(dest="what", required=True)
    c = sub.add_parser("cmd")
    c.add_argument("line")
    s = sub.add_parser("shot")
    s.add_argument("out")
    d = sub.add_parser("decode")
    d.add_argument("--env", default="crowpanel_7inch")
    d.add_argument("text")
    a = ap.parse_args()

    if a.what == "decode":
        decode(a.env, a.text)
    elif a.http:
        if a.what == "cmd":
            print(http_get(a.http, "/harness", pin=a.pin, cmd=a.line).decode())
        else:
            http_shot(a.http, a.pin, a.out)
            print(a.out)
    elif a.port:
        if a.what == "cmd":
            print(serial_cmd(a.port, a.line))
        else:
            serial_shot(a.port, a.out)
            print(a.out)
    else:
        sys.exit("give --port or --http")


if __name__ == "__main__":
    main()
