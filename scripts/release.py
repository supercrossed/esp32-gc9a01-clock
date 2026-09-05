#!/usr/bin/env python3
"""Build the images that go out to other people.

    python scripts/release.py

A normal `pio run` compiles src/config.h into the binary if you have one, and
that file holds a WiFi password - so the images in firmware/ are personal and
are gitignored for good reason. This builds the same firmware with config.h
moved out of the way, checks the result really is clean, and writes it to
dist/ ready to attach to a GitHub release.

Someone flashing one of those images gets a clock that knows nothing: it
opens its own hotspot on first boot and asks for the network and the
location. Which is what you want for anyone who is not you.
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG = os.path.join(ROOT, "src", "config.h")
HIDDEN = CONFIG + ".release-hidden"
DIST = os.path.join(ROOT, "dist")

def pio():
    """PlatformIO is usually not on PATH when it is installed by the VS Code
    extension, so look where it puts itself before giving up on the name."""
    home = os.path.expanduser("~")
    for c in (os.path.join(home, ".platformio", "penv", "Scripts", "platformio.exe"),
              os.path.join(home, ".platformio", "penv", "bin", "platformio")):
        if os.path.exists(c):
            return c
    return "pio"


# The images worth shipping: the clock itself for each board.
TARGETS = [
    ("s3", ["run", "-e", "clock"]),
    ("c3", ["run", "-e", "c3_clock"]),
    ("c6", ["run", "-d", "c6", "-e", "c6_clock"]),
]


# What actually has to stay private. Not every value in config.h: the zone
# string and the unit word are generic and the firmware contains them anyway
# - flagging those would make the check cry wolf and get ignored.
SECRET_KEYS = ("WIFI_SSID", "WIFI_PASS", "WX_LAT", "WX_LON", "WX_LOCATION")


def secrets():
    """The strings that must not appear in a published image."""
    if not os.path.exists(HIDDEN):
        return []
    text = open(HIDDEN, encoding="utf-8", errors="replace").read()
    out = []
    for m in re.finditer(r'#define\s+(\w+)\s+"([^"]+)"', text):
        key, val = m.group(1), m.group(2)
        if key in SECRET_KEYS and len(val) >= 3:
            out.append(val)
    return out


def main():
    hid = False
    if os.path.exists(CONFIG):
        os.rename(CONFIG, HIDDEN)
        hid = True
        print("src/config.h moved aside for the build")

    try:
        os.makedirs(DIST, exist_ok=True)
        exe = pio()
        for board, args in TARGETS:
            print("\n=== %s ===" % board)
            r = subprocess.run([exe] + args, cwd=ROOT)
            if r.returncode != 0:
                print("build failed for %s" % board)
                return 1
            src = os.path.join(ROOT, "firmware", board, "clock.bin")
            if not os.path.exists(src):
                print("no image produced for %s" % board)
                return 1
            dst = os.path.join(DIST, "clock-%s.bin" % board)
            shutil.copy2(src, dst)
            print("  -> %s (%.0f KB)" % (dst, os.path.getsize(dst) / 1024))
    finally:
        if hid:
            # Put it back even if a build threw, or the next ordinary build
            # would silently lose the developer's own settings.
            leaked = []
            for board, _ in TARGETS:
                p = os.path.join(DIST, "clock-%s.bin" % board)
                if not os.path.exists(p):
                    continue
                blob = open(p, "rb").read()
                for s in secrets():
                    if s.encode() in blob:
                        leaked.append((p, s))
            os.rename(HIDDEN, CONFIG)
            print("\nsrc/config.h restored")
            if leaked:
                # Refuse to leave a leaking image lying around where it might
                # be uploaded: this is the one check the whole script exists
                # for, so a failure here deletes rather than warns.
                for p, _ in leaked:
                    if os.path.exists(p):
                        os.remove(p)
                print("REFUSED: settings from config.h were found in the "
                      "images; they have been deleted.")
                return 1
            print("checked: no config.h values appear in any image")

    print("\ndist/ is ready to attach to a release.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
