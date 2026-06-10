// WiFiPhoneControl — control the mist from your phone. No app, no router.
//
// The board becomes its own WiFi access point with a tiny web page:
//   1. Power the board. On your phone, join WiFi "MistMaker" (password
//      "mistmist" — change below).
//   2. Open http://192.168.4.1 in any browser.
//   3. Slide to dim, tap to toggle. Status (water, battery) updates live.
//
// Built for workshops and demos: shows dimming, current-sense water
// detection, and (on the Battery Kit) battery monitoring with a graceful
// low-battery shutdown — when the pack gets critically low the firmware
// stops the mist, disables the 5V boost rail, and deep-sleeps the ESP32
// instead of browning out mid-mist.
//
// Board: Seeed XIAO ESP32-C6 (select XIAO_ESP32C6 in Tools > Board)
// Library: MistMaker >= 1.1.0

#include <MistMaker.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_sleep.h>

// ---- Select your board (uncomment exactly ONE) ----
MistMaker mist(MistMakerBatteryKitV03());
// MistMaker mist(MistMakerExtensionV01());   // no battery -> battery UI hides itself
// MistMaker mist(MistMakerBlockKitV01());
// MistMaker mist(MistMakerLegacyV1());

const char* AP_SSID     = "MistMaker";
const char* AP_PASSWORD = "mistmist";   // >= 8 chars, or "" for an open network

WebServer server(80);

uint8_t targetLevel = 0;
unsigned long lastBatteryCheck = 0;
unsigned long lastProbe = 0;
bool blocked = false;          // true when sense says we must not mist

// ------------------------------------------------------------------ web UI
const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mist Maker</title>
<style>
 body{font-family:system-ui,sans-serif;background:#0e1420;color:#e8eef7;
      display:flex;flex-direction:column;align-items:center;gap:24px;
      padding:32px 16px;margin:0}
 h1{font-weight:600;font-size:1.3rem;margin:0}
 #card{background:#19233a;border-radius:16px;padding:24px;width:100%;
      max-width:360px;display:flex;flex-direction:column;gap:20px}
 button{font-size:1.1rem;padding:14px;border-radius:12px;border:0;
      background:#3b82f6;color:#fff;width:100%}
 button.off{background:#334155}
 input[type=range]{width:100%}
 .row{display:flex;justify-content:space-between;font-size:.95rem;color:#9fb0c9}
 .val{color:#e8eef7}
</style></head><body>
<h1>Programmable Mist Maker</h1>
<div id="card">
 <button id="pwr" onclick="toggle()">...</button>
 <div>
  <div class="row"><span>Mist level</span><span class="val" id="lvlv">0%</span></div>
  <input type="range" id="lvl" min="0" max="255" value="0"
         oninput="setLevel(this.value)">
 </div>
 <div class="row"><span>Water</span><span class="val" id="water">-</span></div>
 <div class="row" id="battrow"><span>Battery</span><span class="val" id="batt">-</span></div>
</div>
<script>
let on=false;
function setLevel(v){fetch('/api/set?level='+v);document.getElementById('lvlv').textContent=Math.round(v/2.55)+'%';}
function toggle(){fetch('/api/set?level='+(on?0:255)).then(refresh);}
function refresh(){fetch('/api/status').then(r=>r.json()).then(s=>{
 on=s.level>0;
 document.getElementById('pwr').textContent=on?'Turn OFF':'Turn ON';
 document.getElementById('pwr').className=on?'':'off';
 document.getElementById('lvl').value=s.level;
 document.getElementById('lvlv').textContent=Math.round(s.level/2.55)+'%';
 document.getElementById('water').textContent=s.water;
 if(s.batt_v>0.5){document.getElementById('batt').textContent=
   s.batt_v.toFixed(2)+' V ('+s.batt_pct+'%)'+(s.batt_low?' LOW!':'');}
 else{document.getElementById('battrow').style.display='none';}
});}
setInterval(refresh,2000);refresh();
</script></body></html>
)HTML";

const char* waterName(MistSenseState s) {
  switch (s) {
    case MIST_WATER_OK:          return "OK";
    case MIST_WATER_LOW:         return "low - refill soon";
    case MIST_DISC_MISSING:      return "no disc";
    case MIST_DISC_DISCONNECTED: return "disc disconnected";
    default:                     return "unknown";
  }
}

void handleStatus() {
  char buf[192];
  const float bv = mist.readBatteryVolts();
  snprintf(buf, sizeof(buf),
           "{\"level\":%u,\"water\":\"%s\",\"batt_v\":%.2f,"
           "\"batt_pct\":%u,\"batt_low\":%s}",
           mist.getLevel(), waterName(mist.senseState()), bv,
           mist.batteryPercent(), mist.batteryLow() ? "true" : "false");
  server.send(200, "application/json", buf);
}

void handleSet() {
  if (server.hasArg("level")) {
    targetLevel = constrain(server.arg("level").toInt(), 0, 255);
    if (!blocked) mist.setLevel(targetLevel);
  }
  server.send(200, "text/plain", "ok");
}

// ------------------------------------------------ graceful low-battery off
void gracefulPowerOff() {
  Serial.println("[BATTERY] Critical - graceful shutdown to avoid brown-out.");
  mist.shutdown();           // mist off, boost rail off, LED off
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  // Sleep forever; recharge + reset (or power cycle) wakes the board.
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  mist.begin();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP \"");
  Serial.print(AP_SSID);
  Serial.print("\" up. Open http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", []() { server.send_P(200, "text/html", PAGE); });
  server.on("/api/status", handleStatus);
  server.on("/api/set", handleSet);
  server.begin();

  mist.probe(); // initial disc + water check
}

void loop() {
  server.handleClient();

  // Battery watchdog (Battery Kit): every 5 s. Critical -> graceful off.
  if (millis() - lastBatteryCheck > 5000) {
    lastBatteryCheck = millis();
    MistBatteryState bs = mist.batteryState();
    if (bs == MIST_BATT_CRITICAL) gracefulPowerOff();
    if (bs == MIST_BATT_LOW) Serial.println("[BATTERY] Low - charge soon.");
  }

  // Water/disc watchdog: probe every 60 s while running (during a brief
  // internal dip the user barely notices), or every 10 s while blocked.
  const unsigned long interval = blocked ? 10000 : 60000;
  if (millis() - lastProbe > interval) {
    lastProbe = millis();
    MistSenseState s = mist.probe();
    if (s == MIST_DISC_MISSING || s == MIST_DISC_DISCONNECTED) {
      if (!blocked) Serial.println("[SENSE] Disc problem - mist stopped.");
      blocked = true;
      mist.turnOff();
    } else if (blocked && s == MIST_WATER_OK) {
      Serial.println("[SENSE] Recovered - mist re-enabled.");
      blocked = false;
      mist.setLevel(targetLevel);
    }
  }
}
