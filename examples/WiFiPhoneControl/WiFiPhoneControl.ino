// WiFiPhoneControl — control the mist from your phone. No app, no router.
//
// The board becomes its own WiFi access point with a tiny web page:
//   1. Power the board. On your phone, join WiFi "MistMaker" (password
//      "mistmist" — change below).
//   2. Open http://192.168.4.1 in any browser.
//   3. Slide to dim, tap to toggle. Status (water) updates live.
//
// Built for workshops and demos: dimming + current-sense water detection over
// a self-hosted WiFi AP — no app, no router, no internet.
//
// Battery: on Battery Kit V0.4 the TPS2116 mux STATUS pin (D8) lets the library
// tell USB from the cell, so low-battery deep-sleep is safe again —
// batteryState() reports CHARGING on USB and only CRITICAL on a dying cell, so
// the board never sleeps while it's plugged in for reflashing. On V0.3 (no ST
// pin) switch the preset below — its preset ships with battery sensing off.
//
// Board: Seeed XIAO ESP32-C6 (select XIAO_ESP32C6 in Tools > Board)
// Library: MistMaker >= 2.1.0

#include <MistMaker.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_sleep.h>          // deep sleep on a critically low cell

// ---- Select your board (uncomment exactly ONE) ----
MistMaker mist(MistMakerBatteryKitV041());  // V0.4/V0.4.1: ST on D8 gates battery vs USB
// MistMaker mist(MistMakerBatteryKitV03()); // V0.3 board (battery sensing off - no ST pin)
// MistMaker mist(MistMakerExtensionV01());
// MistMaker mist(MistMakerBlockKitV01());
// MistMaker mist(MistMakerLegacyV1());

const char* AP_SSID     = "MistMaker";
const char* AP_PASSWORD = "mistmist";   // >= 8 chars, or "" for an open network

WebServer server(80);

uint8_t targetLevel = 0;
unsigned long lastProbe = 0;
unsigned long lastBattMs = 0;
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
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"level\":%u,\"water\":\"%s\"}",
           mist.getLevel(), waterName(mist.senseState()));
  server.send(200, "application/json", buf);
}

void handleSet() {
  if (server.hasArg("level")) {
    targetLevel = constrain(server.arg("level").toInt(), 0, 255);
    if (!blocked) mist.setLevel(targetLevel);
  }
  server.send(200, "text/plain", "ok");
}

// Low-battery guard. batteryState() is ST-gated: it returns MIST_BATT_CHARGING
// on USB, so this only fires on a genuinely dying cell — never while plugged in
// to reflash. On CRITICAL: mist off, radio off, deep-sleep to protect the LiPo
// (recharge + reset/power-cycle wakes it).
void checkBattery() {
  // Require TWO consecutive CRITICAL reads (~10 s) before the irreversible
  // deep-sleep. A single sample can lie: the boost sags BATT+ under mist load,
  // and on a V0.3 board the sense pin floats — neither should strand the board.
  static uint8_t critStreak = 0;
  if (mist.batteryState() != MIST_BATT_CRITICAL) { critStreak = 0; return; }
  if (++critStreak < 2) return;
  Serial.println("[BATTERY] Critical on cell - graceful shutdown.");
  mist.shutdown();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  // On Battery Kit V0.3 (no ST pin) uncomment this so the unreliable D1 reading
  // can't trigger a false low-battery shutdown:
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

  // Low-battery guard every 5 s (no-op on USB / boards without a cell).
  if (millis() - lastBattMs > 5000) {
    lastBattMs = millis();
    checkBattery();
  }
}
