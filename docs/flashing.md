# Putting the clock on a board

You need the board, a USB-C cable, and nothing else. No toolchain, no
compiler, no account.

## The easy way

1. Download **ClockFlasher** from the
   [latest release](https://github.com/supercrossed/esp32-gc9a01-clock/releases/latest).
2. Plug the board into the computer with a USB-C cable.
3. Open ClockFlasher, pick your board and the port it appeared on, and press
   **Download latest and flash**.

That is the whole process. It fetches the current firmware itself, so there
is nothing to download by hand and nothing to unzip.

If no ports are listed, the cable is the usual reason - a good many USB-C
cables carry power only. Try another one before anything else.

If it says it cannot connect to the board: unplug it, hold the **BOOT**
button down, plug it back in, let go, and press flash again. That puts the
chip in its download mode by hand.

## Without the flasher

The release also carries a plain image per board -- `clock-c6.bin`,
`clock-s3.bin`, `clock-c3.bin`. Each is a complete image: bootloader,
partition table and application in one file, written in a single step.

With [esptool](https://github.com/espressif/esptool) installed:

```
esptool --chip esp32c6 --port COM9 write-flash 0x0 clock-c6.bin
```

Substitute `esp32s3` or `esp32c3`, and your own port -- `COM5` on Windows,
`/dev/ttyACM0` on Linux, `/dev/cu.usbmodem...` on macOS.

Espressif's browser flasher at <https://espressif.github.io/esptool-js/>
works too, and needs nothing installed: choose the file, set the offset to
`0`, and connect. Chrome or Edge only.

## First boot: WiFi setup

With nothing saved, the clock opens a hotspot called **Clock-Setup** and the
screen tells you so. Join it from a phone and the setup page pops up on its
own (if it doesn't, open `192.168.4.1`). Pick your network from the list,
type the password, put in a ZIP or town name for the weather, choose °F or
°C, and save. The clock tries the network with the hotspot still up so the
phone is told whether it worked, then closes the hotspot and carries on.

The location is looked up once through Open-Meteo's geocoder, which also
returns the timezone, so there's no timezone to set. Common zones get their
proper daylight-saving rules; anywhere else uses the UTC offset the weather
feed reports, refreshed every ten minutes.

To change any of it later, hold the **BOOT** button while powering on for
three seconds. That wipes the saved settings and opens the hotspot again. It
also reopens by itself, with the clock still running, if the network is gone
for more than a minute.

Everything is stored in the chip's flash, so it survives reflashing the app
unless you erase the whole chip.
