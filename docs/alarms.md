# Alarms

On the Waveshare ESP32-C6 AMOLED board only — it is the one with a
touchscreen and a speaker.

Hold a finger anywhere on the face for about a second to open them.

## Using it

**The list.** Every alarm, with its time and sound. The toggle on the right
of a row turns one on or off without opening it. Drag to scroll if there are
more than fit. `+` adds one, `Done` goes back to the clock.

**The editor.** Tap a row to open it. Hour and minute are two columns you
drag, flick or tap above and below to change — a flick keeps spinning and
eases to a stop. Times are 24 hour, like every digital face here. The strip
below opens the sound picker; `Save` keeps the changes, and the button
beside it is `Cancel` on a new alarm and `Delete` on an existing one.

**Sounds.** Eight of them — Beep, Chime, Radar, Bells, Ascend, Pulse,
Marimba, Siren. Tap one to hear it and select it at the same time. They play
at a fixed low level here, whatever the alarm volume is set to.

**Volume** is on the list screen, not in the editor: one setting for every
alarm. It plays a sample while your finger is on the slider.

**When one goes off** the screen turns over to the ringer whatever was on
it. `Dismiss` stops it. An alarm that has rung will not ring again inside
the same minute.

Up to eight alarms. They are stored in NVS, so they survive a power cut.

## How it works

Sound is synthesised, not sampled — there is no flash to spare beside the
faces, and a softened square wave is what a small class-D speaker reproduces
best anyway. Melodies are lists of note-and-duration pairs in
`src/c6/audio.cpp`; adding one means adding an array, an entry in
`MELODIES[]`, a name, and a value in the `Sound` enum.

The audio path is an **ES8311** codec on I2S into an **NS4150B** class-D
amplifier, out to the speaker connector. There is no DAC on this board and
the ESP32-C6 has no DAC peripheral, so every tone goes out as I2S samples
the codec converts.

Two details that are easy to get wrong if you are working on this:

- The amplifier's enable is **EXIO7 on the TCA9554 expander**, not a GPIO.
  Without driving it the codec plays into a disabled amplifier and you hear
  nothing at all.
- I2S is **IO19 MCLK, IO21 SCLK, IO22 LRCK, IO23 DSDIN**. IO20 is ASDOUT,
  the codec's ADC output — an input to us — and reading the pin table one
  column early puts the bit clock on the codec's output pin, which is silent
  in a way nothing else explains.

Volume maps linearly onto −40…0 dB. The codec's volume register is already
logarithmic at roughly 0.5 dB a step, so applying a percentage-squared curve
on top of it applies a curve twice and leaves the bottom two thirds of the
dial inaudible.

Alarms are checked once a second against local time and fire on the minute.
The clock keeps working normally if the codec never initialises; a failure
is reported on the alarm list rather than being silent about being silent,
because `Serial` goes to UART0 on this board and cannot be read over USB.
