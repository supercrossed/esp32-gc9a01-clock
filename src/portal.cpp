#include "portal.h"
#include "settings.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

namespace portal {

const char *AP_SSID = "Clock-Setup";
static const IPAddress AP_IP(192, 168, 4, 1);

static WebServer server(80);
static DNSServer dns;
static bool      isUp = false;
static bool      routesSet = false;
static State     st = IDLE;
static uint32_t  stateAt = 0;
static String    options;    // <option> list from the last scan

static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Clock setup</title>
<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:24px;max-width:420px}
h1{font-size:22px;margin:0 0 4px}p{color:#aaa;margin:0 0 20px}label{display:block;margin:14px 0 6px;font-size:14px;color:#ccc}
select,input[type=text],input[type=password]{width:100%;box-sizing:border-box;padding:10px;border:1px solid #444;border-radius:8px;background:#222;color:#eee;font-size:16px}
.u{display:flex;gap:20px;margin-top:6px}.u label{display:inline;margin:0}
button{margin-top:22px;width:100%;padding:12px;border:0;border-radius:8px;background:#e8792b;color:#fff;font-size:16px}a{color:#e8792b}</style></head><body>
<h1>Clock setup</h1><p>Pick your WiFi and say where you are.</p>
<form method=post action=/save>
<label>WiFi network</label><select name=ssid>%OPTIONS%</select>
<label>Or type a network name</label><input type=text name=other autocapitalize=none autocorrect=off>
<label>Password</label><input type=password name=pass>
<label>Location, for the weather (ZIP / postcode or town)</label><input type=text name=loc value="%LOC%" placeholder="32839 or Orlando">
<label>Temperature</label><div class=u><label><input type=radio name=units value=f %FCHK%> &deg;F</label><label><input type=radio name=units value=c %CCHK%> &deg;C</label></div>
<button>Save and connect</button></form>
<p style="margin-top:18px"><a href=/scan>Rescan networks</a></p>
</body></html>)HTML";

static const char WAIT[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Connecting</title>
<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:24px;max-width:420px}
h1{font-size:22px;margin:0 0 4px}p{color:#aaa;line-height:1.4}a{color:#e8792b}</style></head><body>
<h1>Connecting</h1><p id=s>Trying the network. This can take up to twenty seconds, and your phone may drop off Clock-Setup for a moment while the radio changes channel.</p>
<script>function poll(){fetch('/status').then(r=>r.text()).then(t=>{var s=document.getElementById('s');
if(t.indexOf('ok')==0){s.innerHTML='Connected. The clock is online at '+t.slice(3)+' and this hotspot closes in a few seconds. Put your phone back on your own WiFi.';}
else if(t=='fail'){s.innerHTML='Could not join that network. Check the password and <a href="/">try again</a>.';}
else setTimeout(poll,1000);}).catch(function(){setTimeout(poll,1000);});}poll();</script></body></html>)HTML";

static String htmlEscape(const String &s)
{
    String o;
    o.reserve(s.length() + 8);
    for (char c : s) {
        switch (c) {
            case '&':  o += "&amp;";  break;
            case '<':  o += "&lt;";   break;
            case '>':  o += "&gt;";   break;
            case '"':  o += "&quot;"; break;
            default:   o += c;
        }
    }
    return o;
}

static void setState(State s) { st = s; stateAt = millis(); }

static void scan()
{
    options = "";
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n && i < 24; i++) {
        String s = WiFi.SSID(i);
        if (!s.length()) continue;
        String e = htmlEscape(s);
        if (options.indexOf("value=\"" + e + "\"") >= 0) continue;   // same SSID, other AP
        options += "<option value=\"" + e + "\">" + e;
        if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) options += " (open)";
        options += "</option>";
    }
    WiFi.scanDelete();
    if (!options.length()) options = "<option value=\"\">(none found, type one below)</option>";
}

static void redirectRoot()
{
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

static void sendPage()
{
    String html = FPSTR(PAGE);
    html.replace("%OPTIONS%", options);
    html.replace("%LOC%", htmlEscape(settings.locQuery));
    html.replace("%FCHK%", settings.useF ? "checked" : "");
    html.replace("%CCHK%", settings.useF ? "" : "checked");
    server.send(200, "text/html", html);
}

static void handleSave()
{
    String ssid = server.arg("other");
    ssid.trim();
    if (!ssid.length()) ssid = server.arg("ssid");
    if (!ssid.length()) {
        server.send(400, "text/plain", "A network name is required. Go back and pick or type one.");
        return;
    }
    settings.ssid = ssid;
    settings.pass = server.arg("pass");

    String loc = server.arg("loc");
    loc.trim();
    if (loc != settings.locQuery) {           // new place: forget the old fix
        settings.locQuery = loc;
        settings.hasLoc   = false;
        settings.locName  = "";
        settings.tzName   = "";
    }
    settings.useF = server.arg("units") != "c";
    settingsSave();

    // Try it with the hotspot still up, so the phone can be told the result.
    WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
    setState(CONNECTING);
    server.send_P(200, "text/html", WAIT);
}

static void handleStatus()
{
    String s = (st == OK)   ? "ok " + WiFi.localIP().toString()
             : (st == FAIL) ? "fail"
             :                "connecting";
    server.send(200, "text/plain", s);
}

void start()
{
    if (isUp) return;

    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect(false);                   // a reconnecting station breaks the scan
    delay(100);
    scan();
    if (settings.hasWifi())                   // keep trying the saved network meanwhile,
        WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());   // it may just be down

    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID);
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", AP_IP);

    if (!routesSet) {
        routesSet = true;
        server.on("/",       HTTP_GET,  sendPage);
        server.on("/save",   HTTP_POST, handleSave);
        server.on("/status", HTTP_GET,  handleStatus);
        server.on("/scan",   HTTP_GET,  [] { scan(); redirectRoot(); });
        // captive-portal probes from every OS land here and get bounced to the page
        server.onNotFound(redirectRoot);
    }
    server.begin();
    isUp = true;
    setState(IDLE);
}

void stop()
{
    if (!isUp) return;
    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    isUp = false;
    setState(IDLE);
}

void handle()
{
    if (!isUp) return;
    dns.processNextRequest();
    server.handleClient();
    if (st == CONNECTING) {
        if (WiFi.status() == WL_CONNECTED)      setState(OK);
        else if (millis() - stateAt > 20000) { setState(FAIL); WiFi.disconnect(false); }
    }
}

bool     running()    { return isUp; }
State    state()      { return st; }
uint32_t sinceState() { return millis() - stateAt; }

} // namespace portal
