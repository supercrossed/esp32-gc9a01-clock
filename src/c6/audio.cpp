#include "audio.h"
#include "i2cbus.h"
#include "expander.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

namespace audio {

// ---- hardware -------------------------------------------------------------
// From the board's GPIO table: IO19 MCLK, IO21 SCLK, IO22 LRCK, IO23 DSDIN.
// Note IO20 is ASDOUT - the codec's ADC output, an input to us - so it is
// deliberately not here. Reading the pins off one column too early puts the
// bit clock on the codec's output and the word clock on its bit clock, and
// nothing is ever heard.
static const int PIN_MCLK = 19, PIN_SCLK = 21, PIN_LRCK = 22, PIN_DSDIN = 23;
static const uint8_t ES8311_ADDR = 0x18;
static const uint32_t RATE = 16000;         // plenty for tones, and light on DMA

static i2s_chan_handle_t tx = nullptr;
static bool started = false;
static const char *statusMsg = "not started";
static bool foundCodec = false, foundExpander = false;

// ---- ES8311 ---------------------------------------------------------------
// Register writes follow the sequence in Espressif's es8311 driver and
// Waveshare's example: reset, clock manager from MCLK, 16-bit I2S in, then
// the DAC on. The part is clocked at 256 x Fs, which is what MCLK gives it.
static bool codecWrite(uint8_t reg, uint8_t val) { return i2cbus::write8(ES8311_ADDR, reg, val); }

static bool codecInit()
{
    if (!i2cbus::present(ES8311_ADDR)) return false;

    codecWrite(0x00, 0x1F);           // reset
    delay(20);
    codecWrite(0x00, 0x00);
    codecWrite(0x00, 0x80);           // out of reset, slave mode

    codecWrite(0x01, 0x3F);           // clock manager: MCLK on, all clocks enabled
    codecWrite(0x02, 0x00);           // no MCLK divider: 256 x Fs at 16 kHz
    codecWrite(0x03, 0x10);
    codecWrite(0x04, 0x10);
    codecWrite(0x05, 0x00);
    codecWrite(0x06, 0x03);           // SCLK = 64 x Fs
    codecWrite(0x07, 0x00);
    codecWrite(0x08, 0xFF);

    codecWrite(0x09, 0x0C);           // SDP in: 16-bit, I2S
    codecWrite(0x0A, 0x0C);           // SDP out: 16-bit, I2S

    codecWrite(0x0B, 0x00);
    codecWrite(0x0C, 0x00);
    codecWrite(0x10, 0x1F);           // analogue power up
    codecWrite(0x11, 0x7F);
    codecWrite(0x00, 0x80);

    codecWrite(0x0D, 0x01);           // power up DAC path
    codecWrite(0x0E, 0x02);
    codecWrite(0x12, 0x00);           // DAC not muted
    codecWrite(0x13, 0x10);
    codecWrite(0x32, 0x00);           // DAC volume, set properly by setVolume()
    codecWrite(0x37, 0x08);
    return true;
}

// ---- volume ---------------------------------------------------------------
static uint8_t volPct = 100;
// The volume to go back to when a preview ends. A preview deliberately does
// not disturb the stored alarm volume.
static uint8_t restoreVol = 100;
static bool    previewing = false;

void setVolume(uint8_t pct)
{
    if (pct > 100) pct = 100;
    volPct = pct;
    if (!started) return;
    // Register 0x32 is already logarithmic: 0xFF is 0 dB and each step down is
    // about 0.5 dB, so the register IS the dB scale. Squaring the percentage
    // on top of that applies a curve twice - it put 70% at roughly -66 dB,
    // which is inaudible, and left the whole bottom two thirds of the dial
    // silent. Map the percentage linearly onto a useful dB span instead.
    //
    // -40 dB at the bottom rather than the register's true floor: below that
    // a small class-D speaker is silent anyway, so those steps would just be
    // more dead travel.
    if (pct == 0) { codecWrite(0x32, 0x00); return; }        // 0 is mute
    const float LO_DB = -40.0f;
    float db  = LO_DB * (float)(100 - pct) / 99.0f;
    int   reg = (int)lroundf(255.0f + db / 0.5f);
    if (reg < 1)   reg = 1;
    if (reg > 255) reg = 255;
    codecWrite(0x32, (uint8_t)reg);
}

// ---- melodies -------------------------------------------------------------
// Synthesised, not sampled: there is no flash to spare beside the faces, and
// a square-ish tone is what a small class-D speaker reproduces best anyway.
static const Note M_BEEP[]  = { {880,150},{0,150},{880,150},{0,550} };
static const Note M_CHIME[] = { {1047,220},{1319,220},{1568,380},{0,500} };
static const Note M_RADAR[] = { {1200,90},{0,60},{1200,90},{0,60},{1200,90},{0,700} };
static const Note M_BELLS[] = { {784,180},{988,180},{1175,180},{988,180},
                                {784,360},{0,600} };
// A rising run, the way a phone alarm eases you awake.
static const Note M_ASCEND[] = { {523,140},{659,140},{784,140},{1047,300},{0,520} };
// Two quick taps on one note, insistent without being shrill.
static const Note M_PULSE[]  = { {988,110},{0,70},{988,110},{0,70},
                                 {988,110},{0,640} };
// Wooden, mid-register: the gentlest of the set.
static const Note M_MARIMBA[] = { {587,150},{880,150},{740,150},{587,260},{0,600} };
// Two-tone, the one that will actually get someone out of bed.
static const Note M_SIREN[]  = { {740,260},{988,260},{740,260},{988,260},{0,360} };

struct Melody { const Note *n; int count; };
static const Melody MELODIES[SOUND_N] = {
    { M_BEEP,    (int)(sizeof M_BEEP    / sizeof(Note)) },
    { M_CHIME,   (int)(sizeof M_CHIME   / sizeof(Note)) },
    { M_RADAR,   (int)(sizeof M_RADAR   / sizeof(Note)) },
    { M_BELLS,   (int)(sizeof M_BELLS   / sizeof(Note)) },
    { M_ASCEND,  (int)(sizeof M_ASCEND  / sizeof(Note)) },
    { M_PULSE,   (int)(sizeof M_PULSE   / sizeof(Note)) },
    { M_MARIMBA, (int)(sizeof M_MARIMBA / sizeof(Note)) },
    { M_SIREN,   (int)(sizeof M_SIREN   / sizeof(Note)) },
};
static const char *NAMES[SOUND_N] = { "Beep", "Chime", "Radar", "Bells",
                                      "Ascend", "Pulse", "Marimba", "Siren" };
const char *soundName(Sound s) { return NAMES[(int)s % SOUND_N]; }

// ---- player ---------------------------------------------------------------
static volatile bool     wantPlay = false;
static volatile Sound    wantSound = BEEP;
static volatile bool     wantBlip = false;
// Bumped by every play()/stop(). The player samples it inside its inner
// loops, so asking for a different sound cuts the current one off rather
// than waiting for the melody to run out - picking the next sound in the UI
// should be heard at once, not after the old one finishes.
static volatile uint32_t generation = 0;
static TaskHandle_t      task = nullptr;

// A note, rendered a block at a time. Phase carries across blocks so the tone
// does not click at the seams.
static void writeTone(uint16_t hz, uint16_t ms, float &phase, bool &abortOut, uint32_t gen)
{
    static int16_t block[256];
    const int total = (int)((uint32_t)RATE * ms / 1000);
    int done = 0;
    const float step = hz ? 2.0f * (float)M_PI * hz / RATE : 0.0f;

    while (done < total) {
        int n = total - done; if (n > 128) n = 128;
        for (int i = 0; i < n; i++) {
            int16_t v = 0;
            if (hz) {
                // Softened square: the odd harmonics of a hard square are
                // harsh on this speaker, a little rounding takes the edge off.
                float s = sinf(phase) + 0.3f * sinf(3 * phase);
                v = (int16_t)(6000.0f * s);
                phase += step;
                if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
            }
            block[2 * i]     = v;    // stereo frames; the amp takes the left
            block[2 * i + 1] = v;
        }
        size_t wrote = 0;
        i2s_channel_write(tx, block, n * 2 * sizeof(int16_t), &wrote, pdMS_TO_TICKS(200));
        done += n;
        // Stopped, or superseded by a different sound.
        if ((!wantPlay && !wantBlip) || generation != gen) { abortOut = true; return; }
    }
}

static void playerTask(void *)
{
    float phase = 0.0f;
    for (;;) {
        if (!wantPlay && !wantBlip) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }

        // Amplifier on only while there is something to hear: it hisses
        // faintly when enabled with an idle input.
        expander::set(expander::PA_CTRL, true);
        i2s_channel_enable(tx);

        uint32_t gen = generation;
        if (wantBlip) {
            wantBlip = false;
            bool ab = false;
            writeTone(1500, 40, phase, ab, gen);
        } else {
            // Re-read the melody every pass: wantSound can change under us.
            bool ab = false;
            while (wantPlay && !ab && generation == gen) {
                const Melody &m = MELODIES[(int)wantSound % SOUND_N];
                for (int i = 0; i < m.count && wantPlay && !ab && generation == gen; i++)
                    writeTone(m.n[i].hz, m.n[i].ms, phase, ab, gen);
            }
        }

        i2s_channel_disable(tx);
        expander::set(expander::PA_CTRL, false);
    }
}

bool begin()
{
    i2cbus::begin();
    foundExpander = expander::begin();
    foundCodec    = i2cbus::present(ES8311_ADDR);
    if (!foundExpander) { statusMsg = "no expander 0x20"; return false; }
    if (!foundCodec)    { statusMsg = "no codec 0x18";    return false; }
    expander::set(expander::PA_CTRL, false);

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    cc.dma_desc_num  = 4;
    cc.dma_frame_num = 256;
    if (i2s_new_channel(&cc, &tx, nullptr) != ESP_OK) { statusMsg = "i2s channel"; return false; }

    i2s_std_config_t sc = {};
    sc.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(RATE);
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO);
    sc.gpio_cfg.mclk = (gpio_num_t)PIN_MCLK;
    sc.gpio_cfg.bclk = (gpio_num_t)PIN_SCLK;
    sc.gpio_cfg.ws   = (gpio_num_t)PIN_LRCK;
    sc.gpio_cfg.dout = (gpio_num_t)PIN_DSDIN;
    sc.gpio_cfg.din  = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(tx, &sc) != ESP_OK) { statusMsg = "i2s std mode"; return false; }

    if (!codecInit()) { statusMsg = "codec init"; return false; }
    statusMsg = "";
    started = true;
    setVolume(volPct);

    // Below the render loop: a late tone is better than a late frame.
    xTaskCreatePinnedToCore(playerTask, "audio", 4096, nullptr, 1, &task, 0);
    return true;
}

bool ready()   { return started; }
const char *status()  { return statusMsg; }
bool sawCodec()       { return foundCodec; }
bool sawExpander()    { return foundExpander; }
bool playing() { return wantPlay; }

void play(Sound s)
{
    if (!started) return;
    if (previewing) { previewing = false; setVolume(restoreVol); }
    wantSound = s;
    wantPlay  = true;
    generation++;          // cut off whatever is sounding now
}

void playPreview(Sound s, uint8_t pct)
{
    if (!started) return;
    if (!previewing) restoreVol = volPct;
    previewing = true;
    setVolume(pct);
    wantSound = s;
    wantPlay  = true;
    generation++;
}

void stop()
{
    wantPlay = false;
    generation++;
    if (previewing) { previewing = false; setVolume(restoreVol); }
}

void blip()
{
    if (!started || wantPlay) return;
    wantBlip = true;
}

} // namespace audio
