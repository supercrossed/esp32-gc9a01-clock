// ---------------------------------------------------------------------------
//  Sound: ES8311 codec (U10, 0x18) on I2S, into an NS4150B class-D amplifier
//  (U11) whose enable is EXIO7 on the expander, out to the speaker on J5.
//
//  There is no DAC on this board and the ESP32-C6 has no DAC peripheral, so
//  every tone goes out as I2S samples the codec converts.
//
//  Pins from the board's GPIO table: IO19 MCLK, IO20 SCLK, IO21 LRCK,
//  IO23 DSDIN (data to the codec). Control shares the I2C bus with touch.
//
//  Tones are synthesised a block at a time on a task, so a ringing alarm
//  costs the render loop nothing.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

namespace audio {

// One step of a melody: a frequency in Hz (0 = silence) and a duration.
struct Note { uint16_t hz; uint16_t ms; };

// A second voice, played under a melody. Times are absolute milliseconds
// from the start of the tune rather than durations, because a bass line does
// not change on the same beats the melody does and stepping both from one
// clock is simpler than interleaving two note lists.
struct Voice { uint16_t hz; uint16_t startMs; uint16_t endMs; };

// The alarm sounds the UI offers. Kept small and synthesised rather than
// sampled: there is no room for audio files beside the faces.
enum Sound { BEEP = 0, CHIME, RADAR, BELLS, ASCEND, PULSE, MARIMBA, SIREN,
             SHANTY, SOUND_N };
const char *soundName(Sound s);

bool begin();
bool ready();

// Why begin() failed, for the on-screen diagnostic. Empty once sound works.
// Serial goes to UART0 on this board, so a failure has to be visible on the
// panel or it cannot be seen at all.
const char *status();
// Bit set of I2C addresses that answered, for the same diagnostic.
bool sawCodec();
bool sawExpander();

// 0..100, applied in the codec. Persisted by the alarm settings.
void setVolume(uint8_t pct);

// Play at a fixed quiet level regardless of the alarm volume, for auditioning
// a sound while setting it. Restores the alarm volume when it stops.
void playPreview(Sound s, uint8_t pct);

// Start a sound looping until stop(). Safe to call when already playing.
void play(Sound s);
void stop();
bool playing();

// One short confirmation blip, for UI taps. Ignored while an alarm rings.
void blip();

} // namespace audio
