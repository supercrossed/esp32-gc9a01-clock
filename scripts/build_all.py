#!/usr/bin/env python3
"""Build every board: the GC9A01 boards (S3, C3) and the Waveshare C6 AMOLED.

    python scripts/build_all.py

Two PlatformIO projects, on purpose. The S3/C3 envs are in the root
platformio.ini on the official espressif32 platform; the C6 is its own
project in c6/ on the pioarduino fork with its own package store, because
both platforms install a package called framework-arduinoespressif32 and
would otherwise overwrite each other on every switch.

Runs with MSYSTEM cleared: Espressif's toolchain installer refuses to run
under Git Bash, which is how the C6 compiler fails to appear on PATH.
"""
import os
import sys
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECTS = [("GC9A01 boards (S3, C3)", ROOT),
            ("AMOLED board (C6)     ", os.path.join(ROOT, "c6"))]


def pio():
    """The PlatformIO CLI: on PATH if we are lucky, else the penv's python."""
    penv = os.path.expanduser("~/.platformio/penv/Scripts/python.exe")
    if os.name != "nt":
        penv = os.path.expanduser("~/.platformio/penv/bin/python")
    if os.path.isfile(penv):
        return [penv, "-m", "platformio"]
    return ["pio"]


def run(project_dir):
    cmd = pio() + ["run", "-d", project_dir]
    env = dict(os.environ)
    env.pop("MSYSTEM", None)
    env["PLATFORMIO_DISABLE_PROGRESSBAR"] = "true"
    print(">", " ".join(cmd), flush=True)
    return subprocess.run(cmd, cwd=ROOT, env=env).returncode


def main():
    results = [(name, run(d)) for name, d in PROJECTS]
    print()
    for name, rc in results:
        print("%s: %s" % (name, "ok" if rc == 0 else "FAILED"))
    return 0 if all(rc == 0 for _, rc in results) else 1


if __name__ == "__main__":
    sys.exit(main())
