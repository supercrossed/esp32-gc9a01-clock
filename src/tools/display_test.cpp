// ---------------------------------------------------------------------------
//  GC9A01 bring-up test  -  build with: pio run -e displaytest -t upload
//
//  Deliberately minimal: no WiFi, no sprite, no NTP, slow SPI. If the panel
//  is wired correctly this cycles obvious full-screen colours. If this stays
//  black, the problem is wiring / SPI, not the clock firmware.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

struct Step { uint16_t color; const char *name; };
static const Step steps[] = {
    { TFT_RED,     "RED"     },
    { TFT_GREEN,   "GREEN"   },
    { TFT_BLUE,    "BLUE"    },
    { TFT_YELLOW,  "YELLOW"  },
    { TFT_WHITE,   "WHITE"   },
};

void setup()
{
    // Serial is UART0 here (ARDUINO_USB_CDC_ON_BOOT=0), not USB-CDC, so
    // writes go to unconnected pins and can never block the render loop.
    Serial.begin(115200);

    tft.init();
    tft.setRotation(0);

    // A visible marker that init ran at all.
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("GC9A01 OK", 120, 120, 4);
    delay(1500);
}

void loop()
{
    for (const auto &s : steps) {
        tft.fillScreen(s.color);

        // contrasting label + a circle that should sit inside the round bezel
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_BLACK, s.color);
        tft.drawString(s.name, 120, 120, 4);
        tft.drawCircle(120, 120, 118, TFT_BLACK);
        tft.drawCircle(120, 120, 60,  TFT_BLACK);

        Serial.printf("[test] %s\n", s.name);
        delay(1200);
    }
}
