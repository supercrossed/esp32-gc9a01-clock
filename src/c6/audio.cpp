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

// ---- the oscillator --------------------------------------------------------
// A quarter-wave sine table rather than sinf(). Three voices at 16 kHz is
// 48000 samples a second and this chip has no FPU, so a softfloat sine per
// voice per sample would cost more CPU than the whole render loop. A table
// lookup with a 16.16 phase accumulator is a couple of adds and a shift.
static const int SINE_BITS = 8, SINE_N = 1 << SINE_BITS;
static int16_t sineTab[SINE_N];
static bool    sineReady = false;

static void sineInit()
{
    if (sineReady) return;
    for (int i = 0; i < SINE_N; i++)
        sineTab[i] = (int16_t)lroundf(sinf(2.0f * (float)M_PI * i / SINE_N) * 32767.0f);
    sineReady = true;
}

// One voice: a phase accumulator in 16.16, stepped by hz * 2^16 / RATE.
struct Osc {
    uint32_t phase = 0, step = 0;
    void set(uint32_t hz) { step = hz ? (uint32_t)(((uint64_t)hz << 16) / RATE) : 0; }
    inline int32_t next()
    {
        if (!step) return 0;
        phase += step;
        // The softened square the single-voice version used: a fundamental
        // plus a third of its third harmonic. Two lookups, no trig.
        uint32_t i1 = (phase >> 8) & (SINE_N - 1);
        uint32_t i3 = ((phase * 3) >> 8) & (SINE_N - 1);
        return sineTab[i1] + sineTab[i3] / 3;
    }
};

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

// Sea Shanty 2. Lifted from the MIDI: the 'Melody' track as the tune,
// with the bass and accordion parts under it as a second and third
// voice. The excerpt starts where the full band comes in - the first
// ten seconds are melody alone, which sounds thin on one speaker.
//
// Thirty-two seconds, two full turns of the theme. The whole piece runs
// past two minutes, which is a performance rather than an alarm, but a
// shorter loop came back round too obviously.
static const Note M_SHANTY[] = {
    { 554, 151}, { 494, 151}, { 554,  74}, {   0, 234}, { 554,  74}, {   0, 234},
    { 554, 151}, { 494, 151}, { 554,  74}, {   0, 234}, { 494,  74}, {   0, 234},
    { 554, 420}, {   0, 196}, { 440, 151}, { 494, 151}, { 554, 151}, { 587, 151},
    { 554,  74}, {   0, 234}, { 494,  74}, {   0, 234}, { 554, 538}, {   0, 692},
    { 554, 151}, { 494, 151}, { 554,  74}, {   0, 234}, { 554,  74}, {   0, 234},
    { 587, 151}, { 554, 151}, { 494,  74}, {   0, 234}, { 440,  74}, {   0, 234},
    { 554, 538}, {   0,  77}, { 554, 151}, { 494, 151}, { 554, 151}, { 587, 151},
    { 554,  74}, {   0, 234}, { 440,  74}, {   0, 234}, { 554, 538}, {   0, 692},
    { 659, 151}, { 587, 151}, { 659,  74}, {   0, 234}, { 659,  74}, {   0, 234},
    { 659, 151}, { 587, 151}, { 659,  74}, {   0, 234}, { 740,  74}, {   0, 234},
    { 659, 538}, {   0,  77}, { 831,  74}, {   0, 234}, { 831, 151}, { 740, 151},
    { 659,  74}, {   0, 234}, { 587,  74}, {   0, 234}, { 659, 538}, {   0, 692},
    { 554, 151}, { 494, 151}, { 554,  74}, {   0, 234}, { 554,  74}, {   0, 234},
    { 494, 151}, { 554, 151}, { 587,  74}, {   0, 234}, { 494,  74}, {   0, 234},
    { 554, 538}, {   0,  77}, { 554, 151}, { 494, 151}, { 554, 151}, { 587, 151},
    { 554,  74}, {   0, 234}, { 440,  74}, {   0, 234}, { 440,  74}, {   0,1157},
    { 554, 151}, { 494, 151}, { 554,  74}, {   0, 234}, { 554,  74}, {   0, 234},
    { 554, 151}, { 494, 151}, { 554,  74}, {   0, 234}, { 494,  74}, {   0, 234},
    { 554, 420}, {   0, 196}, { 440, 151}, { 494, 151}, { 554, 151}, { 587, 151},
    { 554,  74}, {   0, 234}, { 494,  74}, {   0, 234}, { 554, 538}, {   0, 692},
    { 554, 151}, { 494, 151}, { 554,  74}, {   0, 234}, { 554,  74}, {   0, 234},
    { 587, 151}, { 554, 151}, { 494,  74}, {   0, 234}, { 440,  74}, {   0, 234},
    { 554, 538}, {   0,  77}, { 554, 151}, { 494, 151}, { 554, 151}, { 587, 151},
    { 554,  74}, {   0, 234}, { 440,  74}, {   0, 234}, { 554, 538}, {   0, 692},
    { 659, 151}, { 587, 151}, { 659,  74}, {   0, 234}, { 659,  74}, {   0, 234},
    { 659, 151}, { 587, 151}, { 659,  74}, {   0, 234}, { 740,  74}, {   0, 234},
    { 659, 538},
};

static const Voice V_SHANTY_BASS[] = {
    { 110,    0,  539}, {  82,  616, 1154}, { 110, 1231, 1769}, {  55, 1846, 2103},
    {  82, 2154, 2410}, { 110, 2462, 3000}, {  82, 3077, 3616}, { 110, 3692, 4846},
    { 110, 4923, 5462}, {  82, 5539, 6077}, { 110, 6154, 6692}, {  55, 6769, 7026},
    {  82, 7077, 7333}, { 110, 7385, 7923}, {  82, 8000, 8539}, { 110, 8616, 9769},
    {  82, 9846,10385}, {  62,10462,11000}, {  82,11077,11616}, {  62,11692,11949},
    {  41,12000,12257}, {  82,12308,12846}, {  62,12923,13462}, {  82,13539,14077},
    {  62,14154,14410}, {  41,14462,14718}, { 110,14769,15308}, {  82,15385,15923},
    { 110,16000,16539}, {  55,16616,16872}, {  82,16923,17180}, { 110,17231,17769},
    {  82,17846,18385}, { 110,18462,19616}, { 110,19692,20231}, {  82,20308,20846},
    { 110,20923,21462}, {  55,21539,21795}, {  82,21846,22103}, { 110,22154,22692},
    {  82,22769,23308}, { 110,23385,24539}, { 110,24616,25154}, {  82,25231,25769},
    { 110,25846,26385}, {  55,26462,26718}, {  82,26769,27026}, { 110,27077,27616},
    {  82,27692,28231}, { 110,28308,29462}, {  82,29539,30077}, {  62,30154,30692},
    {  82,30769,31308}, {  62,31385,31641}, {  41,31692,31949},
};

static const Voice V_SHANTY_HARM[] = {
    { 220,  308,  564}, { 110,  923, 1180}, { 220, 1539, 1795}, { 110, 2154, 2410},
    { 220, 2769, 3026}, { 110, 3385, 3641}, { 220, 4000, 4257}, { 110, 4616, 4872},
    { 220, 5231, 5487}, { 110, 5846, 6103}, { 220, 6462, 6718}, { 110, 7077, 7333},
    { 220, 7692, 7949}, { 110, 8308, 8564}, { 220, 8923, 9180}, { 110, 9539, 9795},
    { 330,10154,10410}, { 165,10769,11026}, { 330,11385,11641}, { 165,12000,12257},
    { 330,12616,12872}, { 165,13231,13487}, { 330,13846,14103}, { 165,14462,14718},
    { 220,15077,15333}, { 110,15692,15949}, { 220,16308,16564}, { 110,16923,17180},
    { 220,17539,17795}, { 110,18154,18410}, { 220,18769,19026}, { 110,19385,19641},
    { 220,20000,20257}, { 110,20616,20872}, { 220,21231,21487}, { 110,21846,22103},
    { 220,22462,22718}, { 110,23077,23333}, { 220,23692,23949}, { 110,24308,24564},
    { 220,24923,25180}, { 110,25539,25795}, { 220,26154,26410}, { 110,26769,27026},
    { 220,27385,27641}, { 110,28000,28257}, { 220,28616,28872}, { 110,29231,29487},
    { 330,29846,30103}, { 165,30462,30718}, { 330,31077,31333}, { 165,31692,31949},
};

// A tune is a melody line, and optionally one or two voices played under it.
// The accompaniment is in absolute time from the start of the tune, so it
// does not have to break on the melody's beats.
struct Melody {
    const Note  *n;     int count;
    const Voice *v1;    int v1n;
    const Voice *v2;    int v2n;
};
#define MEL(a)          { a, (int)(sizeof a / sizeof(Note)), nullptr, 0, nullptr, 0 }
#define MEL3(a, b, c)   { a, (int)(sizeof a / sizeof(Note)),                           b, (int)(sizeof b / sizeof(Voice)),                           c, (int)(sizeof c / sizeof(Voice)) }

static const Melody MELODIES[SOUND_N] = {
    MEL(M_BEEP),
    MEL(M_CHIME),
    MEL(M_RADAR),
    MEL(M_BELLS),
    MEL(M_ASCEND),
    MEL(M_PULSE),
    MEL(M_MARIMBA),
    MEL(M_SIREN),
    MEL3(M_SHANTY, V_SHANTY_BASS, V_SHANTY_HARM),
};
static const char *NAMES[SOUND_N] = { "Beep", "Chime", "Radar", "Bells",
                                      "Ascend", "Pulse", "Marimba", "Siren",
                                      "Shanty" };
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

// One note of the melody, mixed with whatever the accompanying voices are
// doing across the same span. `elapsed` is where we are in the tune, in
// milliseconds, so the voices can be looked up by absolute time.
static void writeNote(const Melody &m, uint16_t hz, uint16_t ms,
                      uint32_t &elapsed, Osc &lead, Osc &v1, Osc &v2,
                      bool &abortOut, uint32_t gen)
{
    static int16_t block[256];
    const int total = (int)((uint32_t)RATE * ms / 1000);
    int done = 0;

    lead.set(hz);

    while (done < total) {
        int n = total - done; if (n > 128) n = 128;

        // Which accompaniment notes are sounding at this instant. Looked up
        // once a block rather than once a sample: a block is 8 ms, far finer
        // than any note boundary in the tune.
        uint32_t now = elapsed + (uint32_t)((uint64_t)done * 1000 / RATE);
        uint32_t h1 = 0, h2 = 0;
        for (int i = 0; i < m.v1n; i++)
            if (now >= m.v1[i].startMs && now < m.v1[i].endMs) { h1 = m.v1[i].hz; break; }
        for (int i = 0; i < m.v2n; i++)
            if (now >= m.v2[i].startMs && now < m.v2[i].endMs) { h2 = m.v2[i].hz; break; }
        v1.set(h1);
        v2.set(h2);

        // Mixed at unequal weight: the tune has to stay on top of its own
        // accompaniment, and the bass carries further than its level suggests
        // on a speaker this size.
        for (int i = 0; i < n; i++) {
            int32_t v = (lead.next() * 5 + v1.next() * 3 + v2.next() * 2) >> 5;
            if (v >  9000) v =  9000;      // headroom for three voices at once
            if (v < -9000) v = -9000;
            block[2 * i]     = (int16_t)v;   // stereo frames; the amp takes the left
            block[2 * i + 1] = (int16_t)v;
        }
        size_t wrote = 0;
        i2s_channel_write(tx, block, n * 2 * sizeof(int16_t), &wrote, pdMS_TO_TICKS(200));
        done += n;
        // Stopped, or superseded by a different sound.
        if ((!wantPlay && !wantBlip) || generation != gen) { abortOut = true; return; }
    }
    elapsed += ms;
}

static void playerTask(void *)
{
    Osc lead, v1, v2;
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
            uint32_t el = 0;
            static const Melody bare = { nullptr, 0, nullptr, 0, nullptr, 0 };
            writeNote(bare, 1500, 40, el, lead, v1, v2, ab, gen);
        } else {
            // Re-read the melody every pass: wantSound can change under us.
            bool ab = false;
            while (wantPlay && !ab && generation == gen) {
                const Melody &m = MELODIES[(int)wantSound % SOUND_N];
                uint32_t elapsed = 0;
                for (int i = 0; i < m.count && wantPlay && !ab && generation == gen; i++)
                    writeNote(m, m.n[i].hz, m.n[i].ms, elapsed, lead, v1, v2, ab, gen);
            }
        }

        i2s_channel_disable(tx);
        expander::set(expander::PA_CTRL, false);
    }
}

bool begin()
{
    sineInit();
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
