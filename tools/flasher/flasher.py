#!/usr/bin/env python3
"""A window that puts the clock on a board.

    python tools/flasher/flasher.py

For someone who has a board and a USB cable and does not want a toolchain.
It fetches the latest released image from GitHub, finds the board on a serial
port, and writes it - three clicks, no command line.

Everything it needs beyond the standard library is esptool and pyserial,
which pip installs if they are missing. Bundled into a single .exe with
`python tools/flasher/build_exe.py`, it needs nothing at all.

The images it downloads are built without anyone's WiFi details, so a freshly
flashed clock comes up asking for a network rather than holding someone
else's.
"""
import json
import os
import queue
import subprocess
import sys
import threading
import urllib.request

import tkinter as tk
from tkinter import ttk, filedialog

REPO = "supercrossed/esp32-gc9a01-clock"
API = "https://api.github.com/repos/%s/releases/latest" % REPO

# What each board is called, what esptool calls the chip, and the note that
# tells someone which one they have. Ordered with the touchscreen first
# because it is the one most people will be holding.
BOARDS = [
    ("c6", "esp32c6", "Waveshare ESP32-C6 1.43\" AMOLED",
     "466x466 round touchscreen, speaker, battery"),
    ("s3", "esp32s3", "ESP32-S3 Super Mini + GC9A01",
     "240x240 round LCD, wired to the pins in the README"),
    ("c3", "esp32c3", "ESP32-C3 Super Mini + GC9A01",
     "240x240 round LCD, wired to the pins in the README"),
]


def ensure(pkg, mod=None):
    """Import a package, installing it on first run if it is not there."""
    try:
        return __import__(mod or pkg)
    except ImportError:
        subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", pkg])
        return __import__(mod or pkg)


class App:
    def __init__(self, root):
        self.root = root
        root.title("ESP32 Clock Flasher")
        root.resizable(False, False)

        self.log_q = queue.Queue()
        self.image = None          # bytes of the image to write
        self.image_name = ""

        pad = dict(padx=12, pady=6)
        frm = ttk.Frame(root)
        frm.grid(sticky="nsew")

        # ---- board -------------------------------------------------------
        ttk.Label(frm, text="1.  Which board?", font=("", 10, "bold")) \
            .grid(row=0, column=0, columnspan=3, sticky="w", **pad)
        self.board = tk.StringVar(value=BOARDS[0][0])
        for i, (key, _chip, name, note) in enumerate(BOARDS):
            ttk.Radiobutton(frm, text=name, value=key, variable=self.board) \
                .grid(row=1 + i, column=0, columnspan=2, sticky="w", padx=24)
            ttk.Label(frm, text=note, foreground="#666") \
                .grid(row=1 + i, column=2, sticky="w", padx=(0, 12))

        # ---- port --------------------------------------------------------
        r = 1 + len(BOARDS)
        ttk.Label(frm, text="2.  Which port?", font=("", 10, "bold")) \
            .grid(row=r, column=0, columnspan=3, sticky="w", **pad)
        self.port = tk.StringVar()
        self.port_box = ttk.Combobox(frm, textvariable=self.port,
                                     width=44, state="readonly")
        self.port_box.grid(row=r + 1, column=0, columnspan=2, sticky="w", padx=24)
        ttk.Button(frm, text="Refresh", command=self.find_ports) \
            .grid(row=r + 1, column=2, sticky="w")

        # ---- go ----------------------------------------------------------
        r += 2
        ttk.Label(frm, text="3.  Flash it", font=("", 10, "bold")) \
            .grid(row=r, column=0, columnspan=3, sticky="w", **pad)
        self.go = ttk.Button(frm, text="Download latest and flash",
                             command=self.start)
        self.go.grid(row=r + 1, column=0, sticky="w", padx=24)
        ttk.Button(frm, text="Use a local .bin instead...",
                   command=self.pick_file).grid(row=r + 1, column=1, sticky="w")

        self.status = ttk.Label(frm, text="", foreground="#444")
        self.status.grid(row=r + 2, column=0, columnspan=3, sticky="w", **pad)

        self.out = tk.Text(frm, height=12, width=76, state="disabled",
                           font=("Consolas", 9), background="#111",
                           foreground="#ccc")
        self.out.grid(row=r + 3, column=0, columnspan=3, padx=12, pady=(0, 12))

        self.find_ports()
        self.root.after(80, self.drain)

    # ---- helpers ---------------------------------------------------------
    def log(self, s):
        self.log_q.put(s)

    def drain(self):
        try:
            while True:
                s = self.log_q.get_nowait()
                self.out.configure(state="normal")
                self.out.insert("end", s.rstrip() + "\n")
                self.out.see("end")
                self.out.configure(state="disabled")
        except queue.Empty:
            pass
        self.root.after(80, self.drain)

    def find_ports(self):
        serial_tools = ensure("pyserial", "serial.tools.list_ports")
        import serial.tools.list_ports as lp
        items = []
        for p in lp.comports():
            # Espressif's USB-serial bridge and the native USB-JTAG both
            # report this VID, so say which ones are probably the board.
            likely = (p.vid == 0x303A) or ("CP210" in (p.description or "")) \
                or ("CH340" in (p.description or ""))
            items.append("%s  -  %s%s" % (p.device, p.description or "?",
                                          "   <-- likely" if likely else ""))
        self.port_box["values"] = items
        if items and not self.port.get():
            self.port.set(items[0])
        self.status.configure(
            text="No serial ports found - plug the board in and press Refresh."
            if not items else "")

    def selected_port(self):
        v = self.port.get()
        return v.split(" ")[0] if v else ""

    def pick_file(self):
        p = filedialog.askopenfilename(title="Choose a firmware image",
                                       filetypes=[("Firmware", "*.bin")])
        if not p:
            return
        self.image = open(p, "rb").read()
        self.image_name = os.path.basename(p)
        self.log("using local image %s (%d KB)"
                 % (self.image_name, len(self.image) // 1024))
        self.start(skip_download=True)

    # ---- the work --------------------------------------------------------
    def start(self, skip_download=False):
        port = self.selected_port()
        if not port:
            self.status.configure(text="Pick a port first.")
            return
        self.go.configure(state="disabled")
        threading.Thread(target=self.work, args=(port, skip_download),
                         daemon=True).start()

    def download(self, board):
        self.log("asking GitHub for the latest release...")
        req = urllib.request.Request(API, headers={"User-Agent": "clock-flasher"})
        with urllib.request.urlopen(req, timeout=20) as r:
            rel = json.load(r)
        want = "clock-%s.bin" % board
        for a in rel.get("assets", []):
            if a["name"] == want:
                self.log("downloading %s (%s)" % (want, rel.get("tag_name", "")))
                with urllib.request.urlopen(a["browser_download_url"],
                                            timeout=120) as r:
                    return r.read()
        raise RuntimeError(
            "the latest release has no %s. Someone needs to attach one, or "
            "use a local .bin." % want)

    def work(self, port, skip_download):
        try:
            board = self.board.get()
            chip = dict((b[0], b[1]) for b in BOARDS)[board]

            if not skip_download or self.image is None:
                self.image = self.download(board)
                self.image_name = "clock-%s.bin" % board

            tmp = os.path.join(os.path.expanduser("~"), ".clock-flasher.bin")
            with open(tmp, "wb") as f:
                f.write(self.image)

            ensure("esptool")
            self.log("\nwriting %d KB to %s ...\n" % (len(self.image) // 1024, port))

            # The images are merged - bootloader, partition table and app in
            # one file - so this is a single write at offset 0 and there is
            # nothing for the user to get wrong.
            cmd = [sys.executable, "-m", "esptool", "--chip", chip,
                   "--port", port, "--baud", "460800",
                   "write-flash", "0x0", tmp]
            p = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True,
                                 encoding="utf-8", errors="replace")
            for line in p.stdout:
                self.log(line)
            rc = p.wait()
            os.remove(tmp)

            if rc == 0:
                self.log("\nDone. The clock will restart and open a WiFi "
                         "hotspot called Clock-Setup.")
                self.log("Join it from a phone to give it your network.")
            else:
                self.log("\nesptool failed (%d)." % rc)
                self.log("If it could not connect: unplug, hold BOOT, plug "
                         "back in, release, and try again.")
        except Exception as e:                    # noqa: BLE001 - shown to the user
            self.log("\n%s" % e)
        finally:
            self.go.configure(state="normal")


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()
