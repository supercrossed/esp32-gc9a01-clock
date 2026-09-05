#!/usr/bin/env python3
"""Bundle the flasher into one file that needs nothing installed.

    python tools/flasher/build_exe.py

Produces dist/ClockFlasher.exe on Windows, or the equivalent single binary on
macOS and Linux. Someone who is handed that file does not need Python,
PlatformIO, a toolchain, or a terminal - which is the whole point of it.

Run this on the platform you want to ship for; PyInstaller does not
cross-build.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def main():
    try:
        import PyInstaller  # noqa: F401
    except ImportError:
        print("installing PyInstaller...")
        subprocess.run([sys.executable, "-m", "pip", "install", "pyinstaller"],
                       check=True)

    # esptool and pyserial are imported lazily at runtime, so PyInstaller
    # cannot see them by following imports - they have to be named.
    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--onefile", "--windowed",
        "--name", "ClockFlasher",
        "--distpath", os.path.join(ROOT, "dist"),
        "--workpath", os.path.join(HERE, "build"),
        "--specpath", HERE,
        "--hidden-import", "esptool",
        "--hidden-import", "serial",
        "--hidden-import", "serial.tools.list_ports",
        "--collect-all", "esptool",
        os.path.join(HERE, "flasher.py"),
    ]
    print(" ".join(cmd))
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
