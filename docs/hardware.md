# Hardware

Three boards are supported. Any one of them is a complete clock.

## Hardware

- ESP32-S3 Super Mini **or** ESP32-C3 Super Mini. Both have 4 MB flash and
  native USB, and both are supported here. The C3 has no FPU and one core, so
  the animated faces run a little slower on it, but it's fine.
- GC9A01 1.28" 240x240 round LCD, the 7-pin one (VCC GND SCL SDA DC CS RST).
  There's no backlight pin on this variant; it's always lit. If yours has a
  BLK pin, wire it to 3V3 or add `-D TFT_BL=<gpio>` to `platformio.ini`.
- A USB-C cable. That's it, the board powers the display.

## Waveshare ESP32-C6 1.43" AMOLED

The same firmware also builds for the
[Waveshare ESP32-C6-Touch-AMOLED-1.43](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.43),
a 466x466 AMOLED with capacitive touch in a watch-sized case. Nothing to
wire; the panel and touch are on the board.

```
pio run -d c6 -t upload
python scripts/flash.py c6 clock
```

It's a separate PlatformIO project in `c6/`, sharing the same `src/`. The
ESP32-C6 needs Arduino core 3, which the official PlatformIO platform never
got, so that project uses the
[pioarduino](https://github.com/pioarduino/platform-espressif32) fork with
its own package store; the first build downloads a second toolchain. Keeping
it apart matters because both platforms install a package by the same name
and would overwrite each other in one project. `python scripts/build_all.py`
builds every board. On Windows, build the C6 from PowerShell or CMD rather
than Git Bash: Espressif's toolchain installer refuses to run under MSYS and
the compiler never lands on the PATH (the script handles that itself).

What's different on it:

- **Touch.** Swipe for the next or previous face. Hold a finger down for a
  second to open the alarms. BOOT-at-power-on still works too.
- **Alarms.** Up to eight, saved across power cuts, with eight sounds
  through the onboard speaker. Times are set on a drum picker you can drag
  or flick. See [docs/alarms.md](alarms.md).
- **Auto-rotate.** The IMU keeps the face upright, snapping to the nearest
  quarter turn as you turn the watch.
- **Brightness.** Swipe up or down on the face; a drag across the glass
  covers the range. It rides on top of the automatic day/night level, so
  the panel still dims at dusk after you have set it.
- **Real-time clock.** The battery-backed RTC holds the time while the power
  is off, so the clock is right the moment it boots instead of waiting for
  the network. NTP still corrects it and writes it back.
- **Resolution.** The faces are drawn for 240 pixels; here every coordinate
  is scaled to the panel as it's drawn, so hands, rings and ticks come out
  crisp at 466 rather than upscaled. Text is the same bitmap fonts doubled,
  which at this panel's density is the same physical size as on the 1.28".
- **Memory.** A full 466x466 frame is 434 KB and the chip hasn't got it, so
  the screen is rendered in bands. The sweeping-seconds faces redraw only
  small boxes around the hand, at 8 Hz like a 28,800 bph automatic.
- **AMOLED.** Pure blacks are properly off. Brightness drops after sunset.
  Static faces for three hours at a time are within what these panels
  tolerate, but if you're leaving it on a desk for months, shorter rotation
  is kinder.

The WiFi setup hotspot is no longer on the long press; it opens by itself
if the network has been down for a while, as it always did.

`c6_displaytest` is the bring-up check: colour bands, all three fonts, and
a dot that follows your finger.

## Wiring

| display | ESP32-S3 | ESP32-C3 |
|---|---|---|
| VCC | 3V3 | 3V3 |
| GND | GND | GND |
| SCL | GPIO 12 | GPIO 6 |
| SDA | GPIO 11 | GPIO 7 |
| CS | GPIO 10 | GPIO 10 |
| DC | GPIO 13 | GPIO 5 |
| RST | GPIO 9 | GPIO 4 |

On the C3, ignore the SCK/MOSI/SS labels printed on the board. Those are the
Arduino defaults and go through the GPIO matrix, which tops out around 40 MHz,
right where this runs. GPIO 6/7/10 are the chip's native SPI pins.

3V3, not 5V, for the panel.
