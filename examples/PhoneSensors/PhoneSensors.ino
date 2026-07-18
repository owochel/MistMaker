// PhoneSensors — drive the mist from your phone's sensors, over the internet.
//
// This is the "thin client" half of the phone-sensor demo. The phone web app
// (see extras/phone-app/) does ALL the heavy lifting — mic, light, motion,
// face tracking, audio FFT, and choreographing several makers at once. Each
// mist maker just runs this one sketch: it joins a Cloudflare "room" over a
// secure WebSocket, waits for a mist level (0-100), and reports its status
// back. Flash the SAME sketch to every board; each one names itself from its
// MAC address so the phone can tell them apart.
//
// Why the cloud and not the board's own WiFi page? Phone sensors (mic, motion,
// camera) only work on an HTTPS "secure" page, which the board can't serve over
// its local AP. Hosting the app on Cloudflare gives every phone secure sensor
// access with no certificate warnings. Because mist responds slowly (hundreds
// of ms), cloud round-trip jitter is invisible — so all makers simply join the
// same room and the phone keeps them in sync. No ESP-NOW channel juggling.
//
// DEBUGGING: open Serial Monitor at 115200. The board prints its name, WiFi +
// relay status, every level change, and a status line each second. The same
// status (level, current, water, signal) shows live in the web app's MAKERS
// panel, so you can watch a board from USB or from the phone.
//
// Setup:
//   1. Deploy the relay + app once (extras/phone-app/README.md), note its URL.
//   2. Fill in WIFI_SSID / WIFI_PASS / RELAY_HOST below.
//   3. Flash. Open the app URL on your phone. Your maker appears in the list.
//
// Libraries (Library Manager): MistMaker >= 2.1.0,
//   "WebSockets" by Markus Sattler, "ArduinoJson" by Benoit Blanchon (v7)
// Board: Seeed XIAO ESP32-C6 (select XIAO_ESP32C6 in Tools > Board)

#include <MistMaker.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// ---- Select your board (uncomment exactly ONE) ----
MistMaker mist(MistMakerBatteryKitV041());  // V0.4/V0.4.1: ST on D8 gates battery vs USB
// MistMaker mist(MistMakerBatteryKitV03()); // V0.3 board (battery sensing off - no ST pin)
// MistMaker mist(MistMakerExtensionV01());
// MistMaker mist(MistMakerBlockKitV01());

// ---- Your WiFi + your deployed relay ----
const char* WIFI_SSID  = "your-wifi";
const char* WIFI_PASS  = "your-password";
const char* RELAY_HOST = "mistmaker-relay.YOURNAME.workers.dev"; // no https://, no path
const uint16_t RELAY_PORT = 443;
const char* ROOM = "workshop";            // any phone in this room controls this maker (letters/numbers only)

WebSocketsClient ws;
char deviceId[5];                          // last 2 bytes of MAC, e.g. "A4F1"
char deviceName[20];                       // "Mist-A4F1" (shown in the app + Serial)
uint8_t level = 0;                         // current mist level 0-100
unsigned long lastTick = 0, lastCmd = 0, lastProbe = 0;
const unsigned long CMD_TIMEOUT = 6000;    // mist off if no command this long (phone keepalive is ~2 s)

const char* waterName(MistSenseState s) {
  switch (s) {
    case MIST_WATER_OK:          return "ok";
    case MIST_WATER_LOW:         return "low";
    case MIST_DISC_MISSING:      return "no_disc";
    case MIST_DISC_DISCONNECTED: return "disconnected";
    default:                     return "unknown";
  }
}

// level is a 0-100 percentage; the library caps real PWM duty at 50% internally,
// so 100 here is full mist, not 100% duty.
void applyMistPercent(int pct) {
  pct = constrain(pct, 0, 100);
  if (pct == level) return;                // ignore no-op commands
  level = pct;
  mist.setLevel((uint16_t)level * 255 / 100);
  Serial.printf("[CMD] mist level -> %u%%\n", level);   // serial-debug each change
}

// All status fields are numbers or fixed strings, so snprintf is safe here;
// parsing (below) uses a real JSON parser because the input is not ours.
void sendStatus(float currentMa) {
  char buf[160];
  snprintf(buf, sizeof(buf),
    "{\"t\":\"status\",\"level\":%u,\"current_ma\":%.0f,\"water\":\"%s\",\"rssi\":%d}",
    level, currentMa, waterName(mist.senseState()), WiFi.RSSI());
  ws.sendTXT(buf);
}

// Commands from the phone:
//   {"t":"set","target":"all"|"<id>","level":NN}   one or all makers
//   {"t":"multi","levels":{"<id>":NN,...}}         a different level per maker
void onCommand(const char* msg, size_t len) {
  if (len > 384) return;                               // valid commands are tiny
  JsonDocument doc;                                    // ArduinoJson v7: elastic
  if (deserializeJson(doc, msg, len)) return;          // not JSON — ignore
  const char* t = doc["t"];
  if (!t) return;

  if (!strcmp(t, "set")) {
    // target must be absent (= everyone) or a string naming us/"all";
    // a malformed non-string target must NOT read as "everyone".
    JsonVariant tv = doc["target"];
    if (!tv.isNull() && !tv.is<const char*>()) return;
    const char* tgt = tv.as<const char*>();
    const bool forMe = !tgt || !strcmp(tgt, "all") || !strcmp(tgt, deviceId);
    if (forMe && doc["level"].is<int>()) {
      lastCmd = millis();
      applyMistPercent(doc["level"].as<int>());        // constrained 0..100 inside
    }
  } else if (!strcmp(t, "multi")) {
    JsonVariant mine = doc["levels"][deviceId];
    if (mine.is<int>()) {
      lastCmd = millis();
      applyMistPercent(mine.as<int>());
    }
  }
}

void onWsEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("[WS] connected — open the app on your phone to control me");
      break;
    case WStype_DISCONNECTED:
      Serial.println("[WS] disconnected — mist off, will retry every 3 s");
      applyMistPercent(0);                                   // fail-safe: never atomize on a dead link
      break;
    case WStype_ERROR:
      Serial.println("[WS] error");
      break;
    case WStype_TEXT:
      onCommand((const char*)payload, len);
      break;
    default: break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  // (Battery Kit V0.3? Its preset ships with battery sensing off - no ST pin.)
  mist.begin();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(deviceId,   sizeof(deviceId),   "%02X%02X", mac[4], mac[5]);
  snprintf(deviceName, sizeof(deviceName), "Mist-%s", deviceId);

  Serial.println();
  Serial.println("====================================");
  Serial.printf ("  %s  (room: %s)\n", deviceName, ROOM);
  Serial.printf ("  relay: %s\n", RELAY_HOST);
  Serial.println("====================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] joining \"%s\"", WIFI_SSID);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300); Serial.print(".");
    if (millis() - t0 > 20000) { Serial.println(" timeout — restarting"); ESP.restart(); }
  }
  Serial.printf(" ok — ip %s, rssi %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  // Join the room as a device. beginSSL with no cert = TLS without certificate
  // management (fine here — we send mist levels, not secrets).
  char path[120];
  snprintf(path, sizeof(path), "/ws?room=%s&role=device&id=%s&name=%s",
           ROOM, deviceId, deviceName);
  ws.beginSSL(RELAY_HOST, RELAY_PORT, path);
  ws.onEvent(onWsEvent);
  ws.setReconnectInterval(3000);
  ws.enableHeartbeat(15000, 3000, 2);   // keep the link alive through idle gaps

  mist.probe();   // initial disc + water check
  Serial.println("[OK] ready. Watch this Serial log, or the app's MAKERS panel.");
}

void loop() {
  ws.loop();

  // Fail-safe watchdog: a healthy link refreshes lastCmd every ~2 s (the phone's
  // keepalive), so silence past CMD_TIMEOUT means the phone/relay/WiFi dropped —
  // cut the mist rather than atomize forever.
  if (level > 0 && millis() - lastCmd > CMD_TIMEOUT) {
    Serial.println("[WATCHDOG] no commands — mist off");
    applyMistPercent(0);
  }

  // Re-probe water/disc every 30 s so the app's status stays live, and never
  // keep driving a missing or disconnected disc.
  if (millis() - lastProbe > 30000) {
    lastProbe = millis();
    MistSenseState s = mist.probe();
    if (s == MIST_DISC_MISSING || s == MIST_DISC_DISCONNECTED) applyMistPercent(0);
  }

  // Once a second: report status to the app AND print a Serial heartbeat.
  // One shared reading — readCurrentMa blocks for its sample window.
  if (millis() - lastTick > 1000) {
    lastTick = millis();
    const float ma = mist.readCurrentMa(20);
    Serial.printf("[STAT] level=%u%%  current=%.0f mA  water=%s  wifi=%s(%d dBm)\n",
                  level, ma, waterName(mist.senseState()),
                  ws.isConnected() ? "relay-ok" : "no-relay", WiFi.RSSI());
    if (ws.isConnected()) sendStatus(ma);
  }
}
