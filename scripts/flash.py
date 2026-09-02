#!/usr/bin/env python3
"""Flash a prebuilt image from firmware/<board>/ and verify it landed.

    python scripts/flash.py s3 casio           auto-detect the port
    python scripts/flash.py c3 mosaic COM8     name it explicitly
    python scripts/flash.py --list             show what is built

Verification is not optional here on purpose. esptool prints "Hash of data
verified" once per region, so grepping its output for that string reports
success even when the application region did not land - which cost a real
debugging session once. This checks exit codes and then reads the flash back.
"""
import os
import re
import sys
import glob
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHIPS = {"s3": "esp32s3", "c3": "esp32c3"}
PIO = os.path.expanduser("~/.platformio")
ESPTOOL = os.path.join(PIO, "packages", "tool-esptoolpy", "esptool.py")


def images():
    out = {}
    for board in CHIPS:
        d = os.path.join(ROOT, "firmware", board)
        out[board] = sorted(os.path.splitext(os.path.basename(p))[0]
                            for p in glob.glob(os.path.join(d, "*.bin")))
    return out


def detect_port(chip):
    """Find a port whose chip actually matches - both boards share a VID/PID."""
    try:
        listing = subprocess.run([sys.executable, "-m", "serial.tools.list_ports"],
                                 capture_output=True, text=True).stdout
    except Exception:
        listing = ""
    ports = re.findall(r"^(COM\d+|/dev/tty\S+)", listing, re.M)
    for port in ports:
        r = subprocess.run([sys.executable, ESPTOOL, "--port", port, "chip_id"],
                           capture_output=True, text=True)
        if chip.replace("esp32", "ESP32-").upper() in r.stdout.upper() or \
           chip[5:].upper() in r.stdout.upper():
            return port
    return None


def main():
    if "--list" in sys.argv:
        for board, faces in images().items():
            print("%s: %s" % (board, ", ".join(faces) or "(nothing built)"))
        return 0
    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    board, face = sys.argv[1], sys.argv[2]
    port = sys.argv[3] if len(sys.argv) > 3 else None
    if board not in CHIPS:
        print("board must be one of: %s" % ", ".join(CHIPS))
        return 2

    img = os.path.join(ROOT, "firmware", board, face + ".bin")
    if not os.path.isfile(img):
        print("no such image: %s" % img)
        print("built: %s" % ", ".join(images()[board]))
        return 2

    chip = CHIPS[board]
    if port is None:
        print("looking for an %s..." % chip)
        port = detect_port(chip)
        if port is None:
            print("could not find an %s; pass the port explicitly" % chip)
            return 1
        print("found %s on %s" % (chip, port))

    base = [sys.executable, ESPTOOL, "--chip", chip, "--port", port,
            "--baud", "115200", "--before", "default_reset", "--after", "hard_reset"]

    print("writing %s ..." % img)
    if subprocess.run(base + ["write_flash", "-z", "0x0", img]).returncode != 0:
        print("WRITE FAILED")
        return 1

    print("verifying ...")
    if subprocess.run(base + ["verify_flash", "0x0", img]).returncode != 0:
        print("VERIFY FAILED - the board is NOT running this image")
        return 1

    print("OK: %s/%s is on the board, verified by reading it back" % (board, face))
    return 0


if __name__ == "__main__":
    sys.exit(main())
