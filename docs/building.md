# Building from source

Only needed if you want to change something. To just put the clock on a
board, see [flashing.md](flashing.md) - it needs no toolchain at all.

## Building and flashing

You need [PlatformIO](https://platformio.org/) (the CLI or the VS Code
extension). Then:

```
pio run -e clock -t upload                # ESP32-S3
pio run -e c3_clock -t upload             # ESP32-C3
```

There's nothing to configure before building. WiFi and location are entered
from a phone on first boot (see [flashing.md](flashing.md)). If you'd
rather bake defaults in,
copy `src/config.example.h` to `src/config.h`; it's gitignored so a password
never ends up in the repo.

## Release images

`python scripts/release.py` builds the images that go out to other people.
It moves `src/config.h` aside first, so nothing personal is compiled in,
then checks each image for the values that file held and refuses to leave a
leaking one behind. Output lands in `dist/`, ready to attach to a GitHub
release.

This matters: an ordinary `pio run` with a `config.h` present bakes your
WiFi password into the binary. That is why `firmware/*/clock.bin` is
gitignored and why release images are built separately rather than copied
from there.

## Layout

```
src/
  main.cpp             WiFi, NTP, weather, face rotation, touch
  screen.h             the display back end interface
  tft/screen_tft.cpp   GC9A01 boards: TFT_eSPI full-frame sprite
  c6/                  AMOLED board: canvas rasteriser, panel + touch drivers
  portal.cpp           the Clock-Setup hotspot and its page
  settings.cpp         what setup saved, in NVS
  tz.h                 IANA zone name -> POSIX rule
  face.h               face interface, shared weather icons, day/night
  config.example.h     optional compiled-in defaults
  faces/               one file per face
  tools/display_test.cpp
c6/platformio.ini    the AMOLED board's own project (shares src/)
firmware/<board>/    prebuilt images
scripts/
  flash.py             flash a prebuilt image and verify it
  export_firmware.py   post-build hook that produces the merged images
  patch_tft_c3.py      the TFT_eSPI fix for the C3
tools/render/          renders the preview images in docs/faces
docs/faces/            the images above
```

## Weather

From [Open-Meteo](https://open-meteo.com/), which doesn't need an API key.
Temperature, condition code, day/night and today's sunrise and sunset, fetched
every ten minutes over plain HTTP. The same service's geocoder turns the
ZIP or town from setup into coordinates and a timezone. HTTPS was dropped on purpose: the TLS
handshake wanted ~40 KB on top of the 115 KB frame buffer and was failing to
allocate, and the endpoint is fine over HTTP.

On the S3 the fetch runs on the second core so it never interrupts rendering.
On the C3 there's only one core, so there's a short hitch every ten minutes.
