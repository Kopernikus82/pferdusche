//Version vom 30.10.2025
//Pferdedusche – ESP32 / XIAO ESP32-C3

// ---------------- Includes ----------------
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <Update.h>
#include <Adafruit_NeoPixel.h>
#include <time.h>
#include "LedPattern.h"


// ---------------- Vorwärtsdeklarationen (fix gegen Autoprototyping) ----------------
struct BW_ButtonState;                      // unvollständiger Typ genügt
static bool bw_btn_edge_R9a(BW_ButtonState* b);

// ---------------- Hardware-Pins ----------------
#if defined(CONFIG_IDF_TARGET_ESP32C3)
// Seeed XIAO ESP32-C3
constexpr uint8_t PIN_BTN_TRIGGER = 4;   // D2
constexpr uint8_t PIN_BTN_RESET   = 5;   // D3
constexpr uint8_t PIN_RELAY       = 8;  // D8
constexpr uint8_t PIN_LED         = 20;   // D7 (WS2812)
#else
// Klassischer ESP32 (ggf. anpassen)
constexpr uint8_t PIN_BTN_TRIGGER = 18;
constexpr uint8_t PIN_BTN_RESET   = 19;
constexpr uint8_t PIN_RELAY       = 23;
constexpr uint8_t PIN_LED         = 27;  // WS2812
#endif

// --- Feste AP-IP (192.168.4.1) ---
const IPAddress AP_IP (192,168,4,1);
const IPAddress AP_GW (192,168,4,1);
const IPAddress AP_SN (255,255,255,0);

// ---------------- Konstanten ----------------
constexpr uint32_t BTN_DEBOUNCE_MS = 30;      // Entprellen
constexpr uint8_t  NUM_LEDS        = 6;       // 1x WS2812
constexpr uint8_t  LED_BRIGHTNESS  = 125;      // moderat

// ---------------- WS2812 Status-LED ----------------
Adafruit_NeoPixel pixel(NUM_LEDS, PIN_LED, NEO_GRB + NEO_KHZ800);
LedPattern g_anim(pixel, NUM_LEDS);

enum LedMode {LM_BOOT_ORANGE, LM_READY_GREEN, LM_RUN_BLUE_BLINK, LM_LIMIT_RED, LM_LOCK_RED_BLINK, LM_LOCK_BEACON_SIN,LM_RUN_BEACON_SIN};
static LedMode  ledMode       = LM_BOOT_ORANGE;
static bool     ledBlinkState = false;
static uint32_t ledNextToggle = 0;

static inline uint32_t C_RGB(uint8_t r, uint8_t g, uint8_t b) { return pixel.Color(r, g, b); }
static inline void ledApply(uint32_t c) {
  pixel.fill(c, 0, NUM_LEDS);
  pixel.show();
}

// Mappt 0..maxSet dynamisch auf (speed, dtMs)
static void mapBeaconSpeed(uint8_t set, float& speed, uint16_t& dtMs, uint8_t maxSet) {
  const float    SPEED_MIN = 2.0f;
  const float    SPEED_MAX = 5.0f;
  const uint16_t DT_MAX    = 32;
  const uint16_t DT_MIN    = 14;
  if (set > maxSet) set = maxSet;
  float t = (maxSet == 0) ? 0.0f : (float)set / (float)maxSet; // 0..1
  speed = SPEED_MIN + (SPEED_MAX - SPEED_MIN) * t;
  float dtf = (float)DT_MAX - (float)(DT_MAX - DT_MIN) * t;
  dtMs = (uint16_t)(dtf + 0.5f);
}



static void ledSetMode(LedMode m) {
  ledMode = m;
  switch (m) {
    case LM_BOOT_ORANGE:    ledApply(C_RGB(255,128,0)); break;
    case LM_READY_GREEN:    ledApply(C_RGB(0,255,0));   break;
    case LM_LIMIT_RED:      ledApply(C_RGB(255,0,0));   break;

    case LM_RUN_BLUE_BLINK: // (alt) lassen wir drin für Kompatibilität
      ledBlinkState = true; ledApply(C_RGB(0,0,255)); ledNextToggle = millis() + 300; break;

    case LM_LOCK_RED_BLINK: // (alt)
      ledBlinkState = true; ledApply(C_RGB(255,0,0)); ledNextToggle = millis() + 300; break;

    case LM_RUN_BEACON_SIN:
    case LM_LOCK_BEACON_SIN:
      g_anim.reset(); // Beim Start eines Animationsmodus Phase/Timer zurücksetzen
      break;
  }
}

static uint32_t getLockRemainingMs();
static uint32_t getLockTotalMs();
static uint8_t  getBeaconSpeed();
static void mapBeaconSpeed(uint8_t set, float& speed, uint16_t& dtMs, uint8_t maxSet = 10);

static void ledUpdate() {
  // Bestehende Blink-Logik (falls du sie behältst)
  if ((ledMode == LM_RUN_BLUE_BLINK || ledMode == LM_LOCK_RED_BLINK)
      && (int32_t)(millis() - ledNextToggle) >= 0) {
    ledBlinkState = !ledBlinkState;
    ledApply( (ledMode==LM_RUN_BLUE_BLINK)
              ? (ledBlinkState ? C_RGB(0,0,255) : 0)
              : (ledBlinkState ? C_RGB(255,0,0) : 0) );
    ledNextToggle = millis() + 300;
  }


  if (ledMode == LM_RUN_BEACON_SIN) {
    float sp; uint16_t dt;                                  // <-- DEKLARATION
    mapBeaconSpeed(getBeaconSpeed(), sp, dt, 10);           // <-- BELEGEN
    g_anim.beaconSin(0, 64, 255, sp, dt, 1.6f, 1.0f);       // <-- NUTZEN

  } else if (ledMode == LM_LOCK_BEACON_SIN) {
    float baseSp; uint16_t baseDt;                          // <-- DEKLARATION
    mapBeaconSpeed(getBeaconSpeed(), baseSp, baseDt, 10);   // <-- BELEGEN

    uint32_t rem = getLockRemainingMs();
    float total  = (float)getLockTotalMs();
    float frac   = (total > 1.0f) ? (rem / total) : 0.0f;

    float speed  = (baseSp * 0.6f) + (baseSp * 0.9f) * frac;
    uint16_t dt  = (uint16_t)(baseDt + (8 * (1.0f - frac)));
    float intens = 0.35f + 0.65f * frac;

    g_anim.beaconSin(255, 0, 0, speed, dt, 1.8f, intens);
  }


}



// ---------------- Konfiguration (NVS) ----------------
Preferences prefs;
struct Config {
  bool     apMode = true;
  char     apSsid[32]   = "Pferdedusche";
  char     apPass[64]   = "12345678";
  char     staSsid[32]  = "";
  char     staPass[64]  = "";
  char     mqttHost[64] = "";
  uint16_t mqttPort     = 1883;
  char     mqttUser[32] = "";
  char     mqttPass[64] = "";
  char     mqttBase[64] = "Pferdedusche";
  uint32_t pulseMs      = 30 * 1000UL;
  uint32_t dailyMaxMs   = 15 * 60 * 1000UL;
  uint32_t lockMs       = 10 * 1000UL;
  bool     resetButtonEnabled = false;
  uint8_t  ledBrightness = 32;     //0..255
  uint8_t  beaconSpeed = 3;   // 0..5 (Standard: 3)
} cfg;

static void saveConfig() {
  prefs.putBool("apMode", cfg.apMode);
  prefs.putString("apSsid", cfg.apSsid);
  prefs.putString("apPass", cfg.apPass);
  prefs.putString("staSsid", cfg.staSsid);
  prefs.putString("staPass", cfg.staPass);
  prefs.putString("mqttHost", cfg.mqttHost);
  prefs.putUShort("mqttPort", cfg.mqttPort);
  prefs.putString("mqttUser", cfg.mqttUser);
  prefs.putString("mqttPass", cfg.mqttPass);
  prefs.putString("mqttBase", cfg.mqttBase);
  prefs.putUInt("pulseMs", cfg.pulseMs);
  prefs.putUInt("dailyMaxMs", cfg.dailyMaxMs);
  prefs.putUInt("lockMs", cfg.lockMs);
  prefs.putBool("resetBtn", cfg.resetButtonEnabled);
  prefs.putUChar("ledBr", cfg.ledBrightness);
  prefs.putUChar("bcnSpd", cfg.beaconSpeed); 
}


static void loadConfig() {
  cfg.apMode = prefs.getBool("apMode", cfg.apMode);
  prefs.getString("apSsid", cfg.apSsid, sizeof(cfg.apSsid));
  prefs.getString("apPass", cfg.apPass, sizeof(cfg.apPass));
  prefs.getString("staSsid", cfg.staSsid, sizeof(cfg.staSsid));
  prefs.getString("staPass", cfg.staPass, sizeof(cfg.staPass));
  prefs.getString("mqttHost", cfg.mqttHost, sizeof(cfg.mqttHost));
  cfg.mqttPort = prefs.getUShort("mqttPort", cfg.mqttPort);
  prefs.getString("mqttUser", cfg.mqttUser, sizeof(cfg.mqttUser));
  prefs.getString("mqttPass", cfg.mqttPass, sizeof(cfg.mqttPass));
  prefs.getString("mqttBase", cfg.mqttBase, sizeof(cfg.mqttBase));
  cfg.pulseMs      = prefs.getUInt("pulseMs", cfg.pulseMs);
  cfg.dailyMaxMs   = prefs.getUInt("dailyMaxMs", cfg.dailyMaxMs);
  cfg.lockMs       = prefs.getUInt("lockMs", cfg.lockMs);
  cfg.resetButtonEnabled = prefs.getBool("resetBtn", cfg.resetButtonEnabled);
  cfg.ledBrightness = prefs.getUChar("ledBr", cfg.ledBrightness);
  cfg.beaconSpeed = prefs.getUChar("bcnSpd", cfg.beaconSpeed); 
}

// ---------------- Runtime-State ----------------
WebServer   server(80);
WiFiClient  net;
PubSubClient mqtt(net);

static bool     relayOn     = false;
static uint32_t relayOffAt  = 0;
static uint32_t lockUntil   = 0;

struct RunState {
  uint32_t dailyUsedMs     = 0;
  uint32_t lastResetMillis = 0;
  int lastY = 0, lastM = 0, lastD = 0;
} runState;

// --- Helper-Definitionen: greifen auf die echten Globals zu ---
static uint32_t getLockRemainingMs() {
  uint32_t now = millis();
  return (lockUntil > now) ? (lockUntil - now) : 0;
}
static uint32_t getLockTotalMs() {
  return cfg.lockMs;
}

static uint8_t getBeaconSpeed() {
  return cfg.beaconSpeed;
}

extern Adafruit_NeoPixel pixel;
extern LedPattern g_anim;





// ---------------- Zeit & Tages-Reset ----------------
static bool timeSynced() { time_t now = time(nullptr); return now > 1700000000; }
static void getYMD(int &Y,int &M,int &D){ time_t now=time(nullptr); struct tm t; localtime_r(&now,&t); Y=t.tm_year+1900; M=t.tm_mon+1; D=t.tm_mday; }

static void maybeDailyReset() {
  if (timeSynced()) {
    int y,m,d; getYMD(y,m,d);
    if (runState.lastY==0) { runState.lastY=y; runState.lastM=m; runState.lastD=d; }
    if (y!=runState.lastY || m!=runState.lastM || d!=runState.lastD) {
      runState.dailyUsedMs = 0; runState.lastY=y; runState.lastM=m; runState.lastD=d;
    }
  } else if (millis() - runState.lastResetMillis > 24UL*60UL*60UL*1000UL) {
    runState.dailyUsedMs = 0; runState.lastResetMillis = millis();
  }
}
static void resetDailyNow(const char* reason="MANUAL_RESET") {
  runState.dailyUsedMs = 0; runState.lastResetMillis = millis();
  if (timeSynced()) getYMD(runState.lastY, runState.lastM, runState.lastD);
  if (!relayOn) ledSetMode(LM_READY_GREEN);
  if (mqtt.connected()) {
    String t = String(cfg.mqttBase) + "/event"; mqtt.publish(t.c_str(), reason);
    t = String(cfg.mqttBase) + "/state/dailyUsedSec"; mqtt.publish(t.c_str(), "0", true);
  }
}

// ---------------- Buttons ----------------
struct BW_ButtonState {
  uint8_t  pin;
  bool     enabled      = true;
  bool     lastStable   = true;   // HIGH (Pullup)
  bool     reading      = true;
  uint32_t lastDebounce = 0;
};
static BW_ButtonState g_btnTrigger{ PIN_BTN_TRIGGER, true,  true, true, 0 };
static BW_ButtonState g_btnReset  { PIN_BTN_RESET,   false, true, true, 0 };

// Entprellte fallende Flanke (gedrückt bei LOW), Zeiger-Variante
static bool bw_btn_edge_R9a(BW_ButtonState* b) {
  if (!b || !b->enabled) return false;
  bool raw = digitalRead(b->pin); // HIGH = nicht gedrückt
  if (raw != b->reading) { b->reading = raw; b->lastDebounce = millis(); }
  if ((millis() - b->lastDebounce) > BTN_DEBOUNCE_MS) {
    if (b->reading != b->lastStable) {
      b->lastStable = b->reading;
      if (b->lastStable == LOW) return true; // Flanke: gedrückt
    }
  }
  return false;
}

// ---------------- Relaissteuerung ----------------
static void setRelay(bool on) {
  relayOn = on;
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
  if (on) {
    // Neu: Rundum blau statt altem Blink
    ledSetMode(LM_RUN_BEACON_SIN);
  } else {
    if (runState.dailyUsedMs >= cfg.dailyMaxMs) ledSetMode(LM_LIMIT_RED);
    else ledSetMode(LM_READY_GREEN);
  }
  if (mqtt.connected()) {
    String t = String(cfg.mqttBase) + "/state/relay";
    mqtt.publish(t.c_str(), on ? "ON" : "OFF", true);
  }
}


static void startPulse(uint32_t ms) {
  setRelay(true);
  relayOffAt = millis() + ms;
  if (mqtt.connected()) {
    String t = String(cfg.mqttBase) + "/event";
    char buf[64]; snprintf(buf,sizeof(buf),"PULSE_START_%lus",(unsigned long)(ms/1000));
    mqtt.publish(t.c_str(), buf);
  }
}
static void tryTrigger() {
  maybeDailyReset();
  uint32_t now = millis();
  if (!relayOn && now < lockUntil) {
    if (mqtt.connected()) { String t=String(cfg.mqttBase)+"/event"; mqtt.publish(t.c_str(),"BLOCKED_LOCK"); }
    return;
  }
  if (runState.dailyUsedMs >= cfg.dailyMaxMs) {
    ledSetMode(LM_LIMIT_RED);
    if (mqtt.connected()) { String t=String(cfg.mqttBase)+"/event"; mqtt.publish(t.c_str(),"BLOCKED_DAILY_MAX"); }
    return;
  }
  uint32_t remaining = cfg.dailyMaxMs - runState.dailyUsedMs;
  uint32_t ms = cfg.pulseMs;
  if (ms > remaining) ms = remaining;
  if (!relayOn) startPulse(ms);
}

// ---------------- WiFi / MQTT ----------------
static String ipToString(IPAddress ip){ char b[24]; snprintf(b,sizeof(b),"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]); return b; }
static void startAP() {
  WiFi.mode(WIFI_AP);
  bool cfgOk = WiFi.softAPConfig(AP_IP, AP_GW, AP_SN); (void)cfgOk;
  bool apOk  = WiFi.softAP(cfg.apSsid, cfg.apPass);    (void)apOk;
  Serial.print("[AP] IP: "); Serial.println(WiFi.softAPIP()); // 192.168.4.1
}
static bool startSTA(uint32_t timeoutMs=15000) {
  WiFi.mode(WIFI_STA); WiFi.begin(cfg.staSsid, cfg.staPass);
  uint32_t t0=millis(); while (WiFi.status()!=WL_CONNECTED && (millis()-t0)<timeoutMs) delay(100);
  return WiFi.status()==WL_CONNECTED;
}
static uint32_t nextMqttTry = 0;
static void mqttConnectIfNeeded() {
  if (mqtt.connected()) return;
  uint32_t now = millis(); if ((int32_t)(now - nextMqttTry) < 0) return;
  nextMqttTry = now + 5000; if (cfg.mqttHost[0]==0) return;
  mqtt.setServer(cfg.mqttHost, cfg.mqttPort);
  String cid = String("Pferdedusche-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = (cfg.mqttUser[0]==0) ? mqtt.connect(cid.c_str()) : mqtt.connect(cid.c_str(), cfg.mqttUser, cfg.mqttPass);
  if (ok) { String t = String(cfg.mqttBase) + "/state/relay"; mqtt.publish(t.c_str(), relayOn?"ON":"OFF", true); }
}

// ---------------- HTML aus PROGMEM ----------------
static const char CONTROL_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Pferdebewässerung – Übersicht</title>
<style>
body{font-family:system-ui,Segoe UI,Roboto,Arial;background:#0f172a;color:#e5e7eb;margin:0;padding:20px}
.wrap{max-width:560px;margin:0 auto}
.card{background:#111827;border:1px solid #0003;border-radius:14px;padding:16px;margin-bottom:14px}
.h1{font-weight:700;margin:0 0 12px}
.kv{color:#9ca3af}.kv b{color:#e5e7eb}
.slide{position:relative;height:52px;background:#0b1222;border:1px solid #0003;border-radius:999px;display:flex;align-items:center;padding:0 12px;margin:8px 0}
.slide .track{position:absolute;left:0;top:0;bottom:0;width:0;background:linear-gradient(90deg,#22d3ee,#60a5fa);transition:width .12s}
.slide b{position:relative;margin:0 auto;color:#9ca3af;font-weight:600}
.slide input[type=range]{-webkit-appearance:none;width:100%;height:52px;background:transparent;position:absolute;left:0;top:0}
.slide input::-webkit-slider-thumb{-webkit-appearance:none;width:52px;height:52px;border-radius:50%;background:#22d3ee;border:0;box-shadow:0 6px 18px #22d3ee55}
a{color:#22d3ee;text-decoration:none}
</style></head><body><div class=wrap>
<h1 class=h1>Pferdedusche</h1>
<div class=card><div class=kv id=st>Lade Status…</div></div>
<div class=card><form id=act action=/action method=POST>
  <div class=slide><div class=track id=t1></div><b>Zum Auslösen nach rechts schieben</b><input id=s1 type=range min=0 max=100 value=0></div>
  <div class=slide><div class=track id=t2></div><b>Zum Zurücksetzen nach rechts schieben</b><input id=s2 type=range min=0 max=100 value=0></div>
</form></div>
<div class=card><a href=/settings>⚙️ Einstellungen & Update</a></div>
<script>
async function poll(){try{const r=await fetch('/status.json',{cache:'no-store'});if(!r.ok)return;const j=await r.json();
document.getElementById('st').innerHTML=`Relais: <b>${j.relay?'AN':'AUS'}</b><br>Heute: <b>${j.usedSec}s</b> / Max: <b>${j.maxSec}s</b><br>Einschaltsperre: <b>${j.lock}s</b><br>NTP: <b>${j.ntp?'OK':'keine Uhr'}</b><br>Modus: <b>${j.mode}</b> – IP: <b>${j.ip}</b>`;}catch(e){}}
setInterval(poll,1000);poll();
function mk(idT,idS,cmd){const tr=document.getElementById(idT),s=document.getElementById(idS);function u(){tr.style.width=s.value+"%";}
s.addEventListener('input',u);s.addEventListener('change',()=>{if(s.value>95){const f=new FormData();f.append('cmd',cmd);fetch('/action',{method:'POST',body:f});}
setTimeout(()=>{s.value=0;u();},120);});u();}
mk('t1','s1','TRIGGER'); mk('t2','s2','RESET');
</script></div></body></html>)HTML";

// ===== Neues SETTINGS_HTML_HEAD (Styles + Header) =====
static const char SETTINGS_HTML_HEAD[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Pferdedusche – Einstellungen</title>
<style>
:root{--bg:#0f172a;--card:#111827;--muted:#94a3b8;--accent:#22d3ee;--text:#e5e7eb}
*{box-sizing:border-box}
body{font-family:system-ui,Segoe UI,Roboto,Arial;background:var(--bg);color:var(--text);margin:0;padding:20px}
.wrap{max-width:900px;margin:0 auto}
a{color:var(--accent);text-decoration:none}
.h1{display:flex;justify-content:space-between;align-items:center;margin:0 0 16px}
.card{background:var(--card);border:1px solid #0003;border-radius:14px;padding:16px;margin-bottom:14px}
.grid{display:grid;gap:12px}
.g2{grid-template-columns:repeat(2,minmax(0,1fr))}
.field label{display:block;color:var(--muted);margin:6px 0 6px}
.field input,.field select{width:100%;padding:10px;border-radius:12px;border:1px solid #0003;background:#0b1222;color:#0fffff;color:var(--text)}
.btn{appearance:none;border:0;border-radius:12px;padding:12px 16px;font-size:1rem;font-weight:600;cursor:pointer;background:var(--accent);color:#0f172a;box-shadow:0 6px 20px #22d3ee55}
.kv{color:#94a3b8}
.hint{color:#94a3b8;font-size:.95rem}
.hr{height:1px;background:#0003;margin:12px 0}
.sliderRow{display:grid;grid-template-columns:1fr 100px;gap:12px;align-items:center}
.rangeWrap{position:relative}
.track{position:absolute;left:0;top:50%;height:4px;transform:translateY(-50%);background:linear-gradient(90deg,#22d3ee,#60a5fa);width:0;border-radius:999px}
input[type=range]{-webkit-appearance:none;width:100%;background:transparent}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;border-radius:50%;background:#22d3ee;border:0;box-shadow:0 4px 16px #22d3ee55}
.hide{display:none}
.badge{display:inline-block;padding:2px 8px;border-radius:999px;background:#0b1222;border:1px solid #0003;color:#94a3b8;font-size:.9rem}
</style></head><body><div class=wrap>
<div class=h1><h1 style="margin:0">Einstellungen</h1><a href=/>&larr; Übersicht</a></div>)HTML";

// Mini-HTML-Helper
static inline void escOut(const char* s) { String t(s); t.replace("&","&amp;"); t.replace("\"","&quot;"); t.replace("<","&lt;"); server.sendContent(t); }

// ---------------- Webserver-Routen & Handler ----------------
static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", ""); server.sendContent_P(CONTROL_HTML);
}

// ===== handleSettings() =====
static void handleSettings() {
  String ip = cfg.apMode ? ipToString(WiFi.softAPIP()) : ipToString(WiFi.localIP());

  // JS-String-Escaper für Werte aus NVS
  auto escJS = [](const char* s)->String{
    String t = "\"";
    for (const char* p=s; *p; ++p){
      char c=*p;
      if (c=='\\' || c=='\"') { t += '\\'; t += c; }
      else if (c=='\n' || c=='\r') { /* strip */ }
      else t += c;
    }
    t += "\"";
    return t;
  };

  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent_P(SETTINGS_HTML_HEAD);

  // Kopfstatus
  server.sendContent("<div class=card>"
                     "<div class=kv><b>Modus:</b> ");
  server.sendContent(cfg.apMode ? "Access Point" : "Client (WLAN)");
  server.sendContent(" &nbsp; <b>IP:</b> ");
  server.sendContent(ip);
  server.sendContent("</div>");
  if (cfg.apMode)
    server.sendContent("<div class=hint style='margin-top:6px'>Feste AP-IP: <b>192.168.4.1</b></div>");
  server.sendContent("</div>");

  // FORM START
  server.sendContent("<form action=/save method=POST>");

  // ZEITEN (Slider ganz oben)
  server.sendContent(
    "<div class=card><h3 style='margin:0 0 10px'>Zeiten</h3>"
      "<div class='grid'>"

        "<div class='field'>"
          "<label>Auslösezeit (Sek.)</label>"
          "<div class='sliderRow'>"
            "<div class='rangeWrap'>"
              "<div class='track' id='trk_pulse'></div>"
              "<input id='rng_pulse' type=range min=1 max=300 step=1 name=pulseSec>"
            "</div>"
            "<input id='num_pulse' type=number min=1 max=300 step=1>"
          "</div>"
        "</div>"

        "<div class='field'>"
          "<label>Tages-Maximum (Sek.)</label>"
          "<div class='sliderRow'>"
            "<div class='rangeWrap'>"
              "<div class='track' id='trk_daily'></div>"
              "<input id='rng_daily' type=range min=1 max=7200 step=1 name=dailyMaxSec>"
            "</div>"
            "<input id='num_daily' type=number min=1 max=7200 step=1>"
          "</div>"
        "</div>"

        "<div class='field'>"
          "<label>Mindestabstand (Sek.)</label>"
          "<div class='sliderRow'>"
            "<div class='rangeWrap'>"
              "<div class='track' id='trk_lock'></div>"
              "<input id='rng_lock' type=range min=0 max=300 step=1 name=lockSec>"
            "</div>"
            "<input id='num_lock' type=number min=0 max=300 step=1>"
          "</div>"
        "</div>"

        "<div class='field'>"
          "<label>LED-Helligkeit (0–255)</label>"
          "<div class='sliderRow'>"
            "<div class='rangeWrap'>"
              "<div class='track' id='trk_ledbr'></div>"
              "<input id='rng_ledbr' type=range min=0 max=255 step=1 name=ledBr>"
            "</div>"
            "<input id='num_ledbr' type=number min=0 max=255 step=1>"
          "</div>"
        "</div>"

        /* --- Beacon-Geschwindigkeit --- */
        "<div class='field'>"
          "<label>LED-Geschwindigkeit (0–10)</label>"
          "<div class='sliderRow'>"
            "<div class='rangeWrap'>"
              "<div class='track' id='trk_bcnspd'></div>"
              "<input id='rng_bcnspd' type=range min=0 max=10 step=1 name=beaconSpeed>"
            "</div>"
            "<input id='num_bcnspd' type=number min=0 max=10 step=1>"
          "</div>"
        "</div>"

      "</div>"
      "<div class='hr'></div>"
      "<button class=btn type=submit>Speichern</button>"
    "</div>"
  );

  // NETZWERK
  server.sendContent(
    "<div class=card><h3 style='margin:0 0 10px'>Netzwerk</h3>"
      "<div class='grid g2'>"
        "<div class='field'><label>Modus</label>"
          "<select id=modeSel name=apMode>"
            "<option value=1>Access Point</option>"
            "<option value=0>Client (WLAN)</option>"
          "</select>"
        "</div><div></div>"

        "<div class='field apOnly'><label>AP SSID</label><input id=apSsid name=apSsid></div>"
        "<div class='field apOnly'><label>AP Passwort</label><input id=apPass name=apPass></div>"

        "<div class='field staOnly'><label>WLAN SSID</label><input id=staSsid name=staSsid></div>"
        "<div class='field staOnly'><label>WLAN Passwort</label><input id=staPass name=staPass></div>"
      "</div>"
      "<div class='hint'>Im Client-Modus bezieht das Gerät die IP via DHCP vom Router.</div>"
    "</div>"
  );

  // MQTT
  server.sendContent(
    "<div class=card><h3 style='margin:0 0 10px'>MQTT</h3>"
      "<div class='grid g2'>"
        "<div class='field'><label>MQTT Host</label><input id=mqttHost name=mqttHost></div>"
        "<div class='field'><label>MQTT Port</label><input id=mqttPort type=number name=mqttPort></div>"
        "<div class='field'><label>MQTT User</label><input id=mqttUser name=mqttUser></div>"
        "<div class='field'><label>MQTT Pass</label><input id=mqttPass name=mqttPass></div>"
        "<div class='field' style='grid-column:1/-1'><label>Basis-Topic</label><input id=mqttBase name=mqttBase></div>"
      "</div>"
    "</div>"
  );

  // Optionen + zweiter Speichern-Button
  server.sendContent(
    "<div class=card><h3 style='margin:0 0 10px'>Optionen</h3>"
      "<label style='display:flex;gap:8px;align-items:center'>"
        "<input id=resetBtn type=checkbox name=resetBtn value=1> Reset-Taster (Hardware) aktivieren"
      "</label>"
      "<div class='hr'></div>"
      "<button class=btn type=submit>Speichern</button>"
    "</div>"
  );

  // FORM ENDE
  server.sendContent("</form>");

  // Firmware-Update ganz unten
  server.sendContent(
    "<div class=card><h3 style='margin:0 0 10px'>Firmware-Update</h3>"
      "<form id=up method=POST action=/update enctype=multipart/form-data class=grid>"
        "<div class=field><label>Datei (.bin)</label><input type=file name=firmware accept=.bin required></div>"
        "<div><button class=btn type=submit>Update einspielen</button></div>"
      "</form>"
      "<div class='hint'>Während des Updates nicht ausschalten. Nach Erfolg startet das Gerät automatisch neu.</div>"
    "</div>"
  );

  // 1) Statisches Script
  server.sendContent(R"JS(<script>
function initSettings(sv){
  var byId=function(n){return document.getElementById(n);};
  var set=function(id,val){var el=byId(id); if(el) el.value=val;};
  function show(cls,vis){
    var list=document.querySelectorAll('.'+cls);
    for(var i=0;i<list.length;i++){ list[i].classList.toggle('hide',!vis); }
  }
  var modeSel=byId('modeSel');
  function applyMode(){ var ap=(modeSel.value==='1'); show('apOnly',ap); show('staOnly',!ap); }

  function bindPair(idR,idN,idT,min,max){
    var r=byId(idR), n=byId(idN), t=byId(idT);
    function upTrack(){ var pct=((r.value-min)/(max-min))*100; t.style.width=pct+'%'; }
    function s2n(){ n.value=r.value; upTrack(); }
    function n2s(){ var v=parseInt(n.value||0); if(v<min)v=min; if(v>max)v=max; r.value=v; upTrack(); }
    r.addEventListener('input',s2n); n.addEventListener('input',n2s); upTrack();
  }

  modeSel.addEventListener('change',applyMode);

  // Werte setzen
  set('modeSel', String(sv.apMode));          // robust als String
  set('rng_pulse', sv.pulse); set('num_pulse', sv.pulse);
  set('rng_daily', sv.daily); set('num_daily', sv.daily);
  set('rng_lock',  sv.lock);  set('num_lock',  sv.lock);

  set('rng_ledbr', sv.ledBr); set('num_ledbr', sv.ledBr);
  set('rng_bcnspd', sv.bcnSpd); set('num_bcnspd', sv.bcnSpd); // Beacon-Speed

  set('apSsid', sv.apSsid); set('apPass', sv.apPass);
  set('staSsid', sv.staSsid); set('staPass', sv.staPass);

  set('mqttHost', sv.mqttHost); set('mqttPort', sv.mqttPort);
  set('mqttUser', sv.mqttUser); set('mqttPass', sv.mqttPass);
  set('mqttBase', sv.mqttBase);

  var rb=document.getElementById('resetBtn'); if(rb) rb.checked=!!sv.resetBtn;

  // Bindings
  bindPair('rng_pulse','num_pulse','trk_pulse',1,300);
  bindPair('rng_daily','num_daily','trk_daily',1,7200);
  bindPair('rng_lock','num_lock','trk_lock',0,300);
  bindPair('rng_ledbr','num_ledbr','trk_ledbr',0,255);
  bindPair('rng_bcnspd','num_bcnspd','trk_bcnspd',0,10);

  applyMode();
}
</script>)JS");

  // 2) Dynamische Werte: sv-Objekt aus NVS erzeugen
  String sv = "var sv={";
  sv += "apMode:" + String(cfg.apMode ? 1 : 0) + ",";
  sv += "pulse:"  + String(cfg.pulseMs/1000) + ",";
  sv += "daily:"  + String(cfg.dailyMaxMs/1000) + ",";
  sv += "lock:"   + String(cfg.lockMs/1000) + ",";
  sv += "ledBr:"  + String((int)cfg.ledBrightness) + ",";

  // Beacon-Speed sicher begrenzen (0..10)
  int bcn = (int)cfg.beaconSpeed;
  if (bcn < 0) bcn = 0;
  if (bcn > 10) bcn = 10;
  sv += "bcnSpd:" + String(bcn) + ",";

  sv += "apSsid:" + escJS(cfg.apSsid) + ",";
  sv += "apPass:" + escJS(cfg.apPass) + ",";
  sv += "staSsid:" + escJS(cfg.staSsid) + ",";
  sv += "staPass:" + escJS(cfg.staPass) + ",";
  sv += "mqttHost:" + escJS(cfg.mqttHost) + ",";
  sv += "mqttPort:" + String(cfg.mqttPort) + ",";
  sv += "mqttUser:" + escJS(cfg.mqttUser) + ",";
  sv += "mqttPass:" + escJS(cfg.mqttPass) + ",";
  sv += "mqttBase:" + escJS(cfg.mqttBase) + ",";
  sv += "resetBtn:" + String(cfg.resetButtonEnabled ? 1 : 0);
  sv += "};";

  server.sendContent("<script>"+sv+"</script>");

  // 3) Initialisierung mit den Werten
  server.sendContent("<script>initSettings(sv);</script>");

  // Ende HTML
  server.sendContent("</div></body></html>");
}
 



static void setupWeb() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/settings", HTTP_GET, handleSettings);

  server.on("/status.json", HTTP_GET, [](){
    maybeDailyReset();
    uint32_t now = millis();
    uint32_t lockRem = (lockUntil > now) ? (lockUntil - now)/1000UL : 0;
    String ip = cfg.apMode ? ipToString(WiFi.softAPIP()) : ipToString(WiFi.localIP());
    String out = "{";
    out += "\"relay\":";   out += (relayOn?"true":"false"); out += ",";
    out += "\"usedSec\":"; out += String((uint32_t)(runState.dailyUsedMs/1000UL)); out += ",";
    out += "\"maxSec\":";  out += String((uint32_t)(cfg.dailyMaxMs/1000UL)); out += ",";
    out += "\"lock\":";    out += String(lockRem); out += ",";
    out += "\"ntp\":";     out += (timeSynced()?"true":"false"); out += ",";
    out += "\"mode\":\"";  out += (cfg.apMode?"AP":"STA"); out += "\",";
    out += "\"ip\":\"";    out += ip; out += "\"}";
    server.sendHeader("Cache-Control","no-store");
    server.send(200, "application/json", out);
  });

  server.on("/action", HTTP_POST, [](){
    if (server.hasArg("cmd")) {
      String cmd = server.arg("cmd"); cmd.toUpperCase();
      if (cmd == "TRIGGER") tryTrigger();
      else if (cmd == "RESET") resetDailyNow("WEB_RESET");
    }
    server.send(204, "text/plain", "");
  });

  server.on("/save", HTTP_POST, [](){
    // --- Helfer zum Einlesen ---
    auto getS = [&](const char* name, char* dst, size_t n){
      if (server.hasArg(name)) { String v = server.arg(name); v.trim(); v.toCharArray(dst, n); }
    };
    auto clamp = [](long v, long lo, long hi){ if (v<lo) v=lo; if (v>hi) v=hi; return v; };

    // --- Alte Werte merken (für Umschaltlogik) ---
    bool oldAp = cfg.apMode;

    // --- Grundkonfig einlesen ---
    if (server.hasArg("apMode"))       cfg.apMode = (server.arg("apMode").toInt() == 1);

    getS("apSsid",   cfg.apSsid,   sizeof(cfg.apSsid));
    getS("apPass",   cfg.apPass,   sizeof(cfg.apPass));
    getS("staSsid",  cfg.staSsid,  sizeof(cfg.staSsid));
    getS("staPass",  cfg.staPass,  sizeof(cfg.staPass));

    getS("mqttHost", cfg.mqttHost, sizeof(cfg.mqttHost));
    if (server.hasArg("mqttPort"))     cfg.mqttPort = (uint16_t)clamp(server.arg("mqttPort").toInt(), 1, 65535);
    getS("mqttUser", cfg.mqttUser, sizeof(cfg.mqttUser));
    getS("mqttPass", cfg.mqttPass, sizeof(cfg.mqttPass));
    getS("mqttBase", cfg.mqttBase, sizeof(cfg.mqttBase));

    // --- Zeiten / Limits ---
    if (server.hasArg("pulseSec"))     cfg.pulseMs    = (uint32_t)clamp(server.arg("pulseSec").toInt(),    1, 300)  * 1000UL;
    if (server.hasArg("dailyMaxSec"))  cfg.dailyMaxMs = (uint32_t)clamp(server.arg("dailyMaxSec").toInt(), 1, 7200) * 1000UL;
    if (server.hasArg("lockSec"))      cfg.lockMs     = (uint32_t)clamp(server.arg("lockSec").toInt(),     0, 300)  * 1000UL;

    // --- LED-Helligkeit (0..255) ---
    if (server.hasArg("ledBr")) {
      int lb = server.arg("ledBr").toInt();
      lb = (int)clamp(lb, 0, 255);
      cfg.ledBrightness = (uint8_t)lb;
      pixel.setBrightness(cfg.ledBrightness); // sofort anwenden
      pixel.show();
    }

    // Beacon-Speed (0..5)
    if (server.hasArg("beaconSpeed")) {
    int sp = server.arg("beaconSpeed").toInt();
    if (sp < 0) sp = 0; if (sp > 10) sp = 10;
    cfg.beaconSpeed = (uint8_t)sp;
    }


    // --- Reset-Taster-Option ---
    cfg.resetButtonEnabled = (server.hasArg("resetBtn") && server.arg("resetBtn") == "1");

    // --- Persistieren ---
    saveConfig();

    // --- Netzwerk umschalten / reconnecten ---
    String newIp = "";
    if (oldAp != cfg.apMode) {
      if (cfg.apMode) {
        // Wechsel zu AP
        WiFi.disconnect(true, true);
        WiFi.softAPdisconnect(true);
        startAP();
        newIp = ipToString(WiFi.softAPIP());
      } else {
        // Wechsel zu STA
        WiFi.softAPdisconnect(true);
        bool ok = startSTA(15000);
        newIp = ok ? ipToString(WiFi.localIP()) : "(keine Verbindung)";
      }
    } else {
      // Modus gleich geblieben → aktuelle IP ausgeben
      newIp = cfg.apMode ? ipToString(WiFi.softAPIP()) : ipToString(WiFi.localIP());
    }

    // --- LED-Zustand nach Settings auffrischen ---
    if (!relayOn && runState.dailyUsedMs < cfg.dailyMaxMs) ledSetMode(LM_READY_GREEN);
    else if (runState.dailyUsedMs >= cfg.dailyMaxMs)       ledSetMode(LM_LIMIT_RED);

    // --- Hübsche Erfolgsseite ---
    String resp = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Einstellungen gespeichert</title>
<style>
:root{--bg:#0f172a;--card:#111827;--muted:#94a3b8;--accent:#22d3ee;--text:#e5e7eb}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,Segoe UI,Roboto,Arial}
.wrap{max-width:720px;margin:0 auto;padding:24px}
.card{background:var(--card);border:1px solid #0003;border-radius:16px;padding:20px;box-shadow:0 8px 24px #00000040}
.h1{display:flex;gap:10px;align-items:center;margin:0 0 10px;font-size:1.25rem}
.badge{display:inline-flex;align-items:center;justify-content:center;width:28px;height:28px;border-radius:999px;background:linear-gradient(90deg,#22d3ee,#60a5fa);color:#0f172a;font-weight:800}
.kv{color:var(--muted);margin-top:6px}
.btns{display:flex;gap:12px;margin-top:16px;flex-wrap:wrap}
.btn{appearance:none;border:0;border-radius:12px;padding:12px 16px;font-size:1rem;font-weight:600;cursor:pointer;text-decoration:none;display:inline-flex;align-items:center;gap:8px}
.btn-primary{background:var(--accent);color:#0f172a;box-shadow:0 6px 18px #22d3ee55}
.btn-ghost{background:#0b1222;border:1px solid #0003;color:var(--text)}
.btn:active{transform:translateY(1px)}
.small{color:var(--muted);font-size:.95rem;margin-top:10px}
</style></head><body><div class="wrap">
  <div class="card">
    <div class="h1"><span class="badge">✓</span> Einstellungen gespeichert</div>
    <div class="kv">Aktueller Modus: <b>)HTML";

    resp += (cfg.apMode ? "Access Point" : "Client (WLAN)");
    resp += "</b>";
    if (newIp.length()) {
      resp += " – IP: <b>" + newIp + "</b>";
    }
    resp += R"HTML(</div>
    <div class="btns">
      <a class="btn btn-primary" href="/settings">← Zurück zu den Einstellungen</a>
      <a class="btn btn-ghost" href="/">Zur Übersicht</a>
    </div>
    <div class="small">Tipp: Im AP-Modus erreichst du das Gerät direkt unter <b>http://192.168.4.1/</b>.</div>
  </div>
</div>
</body></html>)HTML";

    server.send(200, "text/html", resp);
  });




  server.begin();

  if (!relayOn && runState.dailyUsedMs < cfg.dailyMaxMs) ledSetMode(LM_READY_GREEN);
  else if (runState.dailyUsedMs >= cfg.dailyMaxMs)       ledSetMode(LM_LIMIT_RED);
}



// ---------------- Setup / Loop ----------------
void setup() {
  Serial.begin(115200);

  // LED
  pixel.begin();
  pixel.setBrightness(cfg.ledBrightness);
  pixel.clear(); pixel.show();
  ledSetMode(LM_BOOT_ORANGE);

  // IO
  pinMode(PIN_RELAY, OUTPUT); setRelay(false);
  pinMode(PIN_BTN_TRIGGER, INPUT_PULLUP);
  pinMode(PIN_BTN_RESET,   INPUT_PULLUP);

  // Config
  prefs.begin("Pferdedusche", false);
  loadConfig();
  g_btnReset.enabled = cfg.resetButtonEnabled;

  // Netzwerk
  if (cfg.apMode) startAP(); else startSTA(8000);

  // Zeit
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
  configTime(0,0,"pool.ntp.org","time.nist.gov");

  // MQTT
  mqtt.setKeepAlive(30); mqtt.setSocketTimeout(5);

  // Webserver
  setupWeb();

  // Initial konsistent
  maybeDailyReset();
}

void loop() {
  // --- Web & LED Ticker ---
  server.handleClient();
  ledUpdate();

  // --- Buttons ---
  if (bw_btn_edge_R9a(&g_btnTrigger))  tryTrigger();
  g_btnReset.enabled = cfg.resetButtonEnabled;
  if (bw_btn_edge_R9a(&g_btnReset))    resetDailyNow("BUTTON_RESET");

  // --- Relais nonblocking + Sperrzeit setzen ---
  uint32_t now = millis();
  if (relayOn && (int32_t)(now - relayOffAt) >= 0) {
    setRelay(false);
    if (cfg.lockMs > 0) lockUntil = now + cfg.lockMs;  // Start Sperrzeit
  }

  // --- Tagesverbrauch akkumulieren ---
  static uint32_t lastTick = now;
  uint32_t dt = now - lastTick; 
  lastTick = now;
  if (relayOn) runState.dailyUsedMs += dt;

  // --- Tageswechsel / Reset-Logik ---
  maybeDailyReset();

  // --- MQTT (nur im STA-Modus) ---
  if (!cfg.apMode && WiFi.status() == WL_CONNECTED) {
    mqttConnectIfNeeded();
  }

// LED-Statusmaschine (nur wenn Relais AUS)
if (!relayOn) {
  if (runState.dailyUsedMs >= cfg.dailyMaxMs) {
    if (ledMode != LM_LIMIT_RED)           ledSetMode(LM_LIMIT_RED);          // Limit: dauerhaft rot
  } else if ((int32_t)(lockUntil - now) > 0) {
    if (ledMode != LM_LOCK_BEACON_SIN)     ledSetMode(LM_LOCK_BEACON_SIN);    // Sperre: rotes Rundum
  } else {
    if (ledMode != LM_READY_GREEN)         ledSetMode(LM_READY_GREEN);        // bereit: grün
  }
}


  // winziger Atemzug für den Scheduler
  delay(1);
}

