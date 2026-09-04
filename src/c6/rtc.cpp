#include "rtc.h"
#include "i2cbus.h"
#include <Wire.h>

namespace rtc {

// Registers.
static const uint8_t REG_CTRL1   = 0x00;
static const uint8_t REG_SECONDS = 0x04;   // seconds .. years run consecutively

// Bit 7 of the seconds register is the oscillator-stop flag: set when the
// part has lost power, which means whatever it holds is meaningless.
static const uint8_t OS_FLAG = 0x80;

static bool ok = false, lastValid = false;

static inline uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static inline uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

bool begin()
{
    i2cbus::begin();
    ok = i2cbus::present(ADDR);
    if (!ok) return false;
    // Leave CTRL1 alone apart from making sure the oscillator is running and
    // the part is not in 12-hour mode; the defaults are right on this board.
    uint8_t c1 = 0;
    if (i2cbus::read8(ADDR, REG_CTRL1, c1)) {
        uint8_t want = (uint8_t)(c1 & ~0x20);      // clear STOP
        if (want != c1) i2cbus::write8(ADDR, REG_CTRL1, want);
    }
    return true;
}

bool present() { return ok; }
bool valid()   { return lastValid; }

time_t readUTC()
{
    lastValid = false;
    if (!ok) return 0;

    uint8_t b[7];
    {
        i2cbus::Hold h;
        Wire.beginTransmission(ADDR);
        Wire.write(REG_SECONDS);
        if (Wire.endTransmission(false) != 0) return 0;
        if (Wire.requestFrom((int)ADDR, 7) != 7) return 0;
        for (int i = 0; i < 7; i++) b[i] = Wire.read();
    }

    if (b[0] & OS_FLAG) return 0;              // lost power: nothing to trust

    struct tm t = {};
    t.tm_sec  = bcd2bin((uint8_t)(b[0] & 0x7F));
    t.tm_min  = bcd2bin((uint8_t)(b[1] & 0x7F));
    t.tm_hour = bcd2bin((uint8_t)(b[2] & 0x3F));
    t.tm_mday = bcd2bin((uint8_t)(b[3] & 0x3F));
    // b[4] is the weekday, which mktime recomputes.
    t.tm_mon  = (int)bcd2bin((uint8_t)(b[5] & 0x1F)) - 1;
    t.tm_year = (int)bcd2bin(b[6]) + 100;      // the part holds 00..99 = 2000..2099

    if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31) return 0;

    // The register contents are UTC. mktime() would apply whatever zone is
    // currently set and timegm() is absent from this newlib, so the epoch is
    // computed here - days since 1970 by Howard Hinnant's civil-day formula,
    // which needs no tables and no leap-year special cases.
    int      yr  = t.tm_year + 1900;
    unsigned mth = (unsigned)t.tm_mon + 1;
    unsigned dy  = (unsigned)t.tm_mday;
    yr -= mth <= 2;
    const int      era = (yr >= 0 ? yr : yr - 399) / 400;
    const unsigned yoe = (unsigned)(yr - era * 400);
    const unsigned doy = (153u * (mth + (mth > 2 ? -3 : 9)) + 2u) / 5u + dy - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    const long     days = (long)era * 146097L + (long)doe - 719468L;

    time_t v = (time_t)(days * 86400L + t.tm_hour * 3600L + t.tm_min * 60L + t.tm_sec);
    if (v < 1700000000) return 0;              // before this project existed
    lastValid = true;
    return v;
}

bool writeUTC(time_t when)
{
    if (!ok) return false;
    struct tm t;
    gmtime_r(&when, &t);

    i2cbus::Hold h;
    Wire.beginTransmission(ADDR);
    Wire.write(REG_SECONDS);
    Wire.write(bin2bcd((uint8_t)t.tm_sec));     // writing clears the OS flag
    Wire.write(bin2bcd((uint8_t)t.tm_min));
    Wire.write(bin2bcd((uint8_t)t.tm_hour));
    Wire.write(bin2bcd((uint8_t)t.tm_mday));
    Wire.write(bin2bcd((uint8_t)t.tm_wday));
    Wire.write(bin2bcd((uint8_t)(t.tm_mon + 1)));
    Wire.write(bin2bcd((uint8_t)(t.tm_year - 100)));
    return Wire.endTransmission() == 0;
}

} // namespace rtc
