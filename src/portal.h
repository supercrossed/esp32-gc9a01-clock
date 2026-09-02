// ---------------------------------------------------------------------------
//  Captive setup hotspot. Brings up an open access point, answers every DNS
//  query with its own address so a phone pops the setup sheet on joining,
//  and serves one page: pick a network, type the password, say where you
//  are. Saving tries the network with the hotspot still up so the phone
//  gets told whether it worked.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

namespace portal {

enum State { IDLE, CONNECTING, OK, FAIL };

extern const char *AP_SSID;

void     start();        // hotspot up; also (re)tries the saved network if any
void     stop();         // hotspot down, back to plain station mode
void     handle();       // call from the loop, often
bool     running();
State    state();
uint32_t sinceState();   // ms since the state last changed

} // namespace portal
