# The faces

All twelve are compiled into one binary. Every three hours the clock switches
to a different face picked at random, never the one already showing, and it
starts on a random one at boot. The interval is `ROTATE_MS` at the top of
`src/main.cpp`; set it to 0 to stay on one face, or set `ROTATE_RANDOM` to
false to go through `ROTATION[]` in order instead.

To look at one face without waiting for the rotation to reach it, build a
preview pinned to it. The preview envs skip the `firmware/` export so they
can't be mistaken for a release image:

```
PREVIEW_FACE=FACE_PCB pio run -e c3_preview -t upload
```

On PowerShell that's `$env:PREVIEW_FACE="FACE_PCB"; pio run -e c3_preview -t upload`.
Use `preview` instead of `c3_preview` for the S3.

- **word** - the QLOCKTWO-style letter grid. Five-minute resolution, with
  four dots around the bottom rim for the remaining minutes. The grid is
  shaped to the circle, so rows get shorter toward the top and bottom.
- **casio** - angled panels like a dive watch LCD, with ghosted unlit
  segments. Day of week is 14-segment so M and W render properly.
- **mosaic** - a grid of pastel tiles that slowly drift in hue and breathe in
  brightness, with the time as dark tiles in the middle.
- **retro** - green LCD strip with a week row above and sunrise/sunset below.
  Grey by day, glows green at night.
- **dotmatrix** - a 70s red LED array. The unlit dots stay faintly visible,
  which is what makes it look like a real one.
- **pulsar** - the other 70s LED watch, the Pulsar P2 / Kingsonic type. Each
  segment is a row of tiny discrete LEDs with a soft halo rather than a solid
  bar, the digits are small in a big black window, and there's nothing else
  on the face. Leading zero blanked below ten, colon blinks.
- **pcb** - a bare circuit board under red glass with four DIP seven-segment
  LED modules soldered across it. Pins, bubble lenses with the segment
  shadows showing, SMD parts with their reference designators, traces and
  vias, a DIP switch marked AM/PM, and four corner LEDs lighting the board.
  The detail is the point; strip it back and it becomes an icon.
- **default** - plain analog with a sweeping second hand and the weather
  beside the 9 and 3.
- **classic** - ivory dress dial. Railroad minute track, upright Roman
  numerals, a framed date window at 3 in place of the III, small seconds at 6
  in place of the VI, and blued Breguet hands with the hollow moon near the
  tip. No centre seconds hand; the seconds live in the sub-dial.
- **modern** - charcoal sports dial with baton indices, broad lumed hands and
  an orange centre seconds. Weather in a sub-dial at 9, the date in a tile at
  3 with the weekday over it, and at 6 a sun or moon for the time of day with
  the next sunrise or sunset time under it.
- **panel** - a segmented LCD cut into a dozen chamfered panels on a dark
  textured ground, each with its own readout, label and icon: sunrise and
  sunset times, temperature, WiFi signal, month and weekday, moon phase,
  the date, daylight length, the GMT offset, and a scale across the top
  whose red marker tracks the day from sunrise to sunset. Grey by day, mint
  after dark.
- **delorean** - the DeLorean's time circuits. Three rows of seven-segment
  readouts on black wells under white-on-red label tabs, red for the top row,
  green for the middle, amber for the bottom, with the flux capacitor
  flickering through its three arms and the plutonium chamber gauge swinging
  a needle. The health readouts of the original become year, sunrise, sunset,
  daylight length, temperature and conditions; the month is spelled out.

All the digital ones are 24-hour.

Adding a face: copy one of the existing `src/faces/face_*.cpp`, keep it in
its own namespace, export a `FaceVTable`, and add it to `ROTATION[]`. Nothing
else needs to change. Each face is a few KB; linking all twelve costs about 2 KB
over a single one because everything else is shared.
