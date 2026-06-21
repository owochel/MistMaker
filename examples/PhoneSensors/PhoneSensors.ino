// PhoneSensors — drive the mist from your phone's sensors, over the internet.
//
// This is the "thin client" half of the phone-sensor demo. The phone web app
// (see extras/phone-app/, hosted at mistcontrol.byproductlab.com) does ALL the
// heavy lifting — mic, light, motion, face tracking, audio FFT, and
// choreographing several makers at once. Each mist maker just runs this sketch:
// it joins a Cloudflare "room" over a secure WebSocket, waits for a mist level
// (0-100), and reports its status back. Flash the SAME sketch to every board.
//
// FIRST-RUN WIFI SETUP (no hardcoding): on first boot — or whenever you hold the
// button at power-on — the board starts its own WiFi "MistMaker-Setup-XXXX".
// Join it, open http://192.168.4.1, name the maker, set its room (must match the
// app), pick the venue WiFi + password, and Save. Settings persist in flash. The
// setup page also has a "Test mist (30%)" button so you can confirm the hardware
// works before any WiFi/cloud.
//
// ROOMS: a room groups the makers a phone controls — same room name = shared
// control, different rooms are isolated. Each board defaults to its OWN unique
// two-word room (e.g. "fluffy-otter", printed in setup + Serial) so a fresh
// board is private — nobody can drive it without knowing its words. For a group,
// set every board to one shared name (e.g. "workshop"). Two ways to change a
// board's room: (1) over-the-air from the app ("Add a maker by its words") with
// NO reboot — a {t:"room"} command authorized by the printed words; (2) hardware
// fallback — hold the button at power-on to re-run the WiFi setup page.
//
// BUTTON: press the board button any time — in WiFi-setup OR connected mode — to
// toggle a local 30% mist (works even with no WiFi/cloud); when connected, the
// change syncs to the app instantly. Hold it at power-on to (re-)run WiFi setup.
//
// SAFETY: the disc's own current draw is sensed every 20 s (no extra parts) —
// a missing/loose disc auto-shuts-off the mist, and low water is flagged live
// in the app. Never runs a dry/absent disc.
//
// LED: the onboard LED (D7) is PWM-dimmed to this maker's mist level — so in
// Music mode it pulses with the maker's assigned frequency band.
//
// Why the cloud and not the board's own page for the demo? Phone sensors
// (mic/motion/camera) only work on an HTTPS "secure" page, which the board
// can't serve over its AP. Hosting the app on Cloudflare gives every phone
// secure sensor access. Mist responds slowly, so all makers just join the same
// room and the phone keeps them in sync — no ESP-NOW needed.
//
// DEBUGGING: Serial Monitor @ 115200 prints WiFi/relay state, every level
// change, and a 1 Hz status line. The same status shows in the app's MAKERS
// panel.
//
// Library: MistMaker >= 1.1.0  +  "WebSockets" by Markus Sattler (Library Mgr)
// Board:   Seeed XIAO ESP32-C6.  NOTE: this sketch is large (TLS + web server) —
//          if it doesn't fit, set Tools > Partition Scheme > "Huge APP (3MB)".

#include <MistMaker.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <Preferences.h>
#include <DNSServer.h>

// ---- Select your board (uncomment exactly ONE) ----
MistMaker mist(MistMakerBatteryKitV03());
// MistMaker mist(MistMakerExtensionV01());
// MistMaker mist(MistMakerBlockKitV01());

// ---- Your deployed relay (the web app's host) ----
const char* RELAY_HOST = "mistcontrol.byproductlab.com"; // no https://, no path
const uint16_t RELAY_PORT = 443;
const char* SHARED_ROOM_EG = "workshop";  // example shared room for a group (set per board in setup)
const int BUTTON = D6;                    // press = mist on/off; hold at power-on = re-run WiFi setup
const uint8_t BUTTON_LEVEL = 30;          // mist % the board button toggles to

Preferences prefs;
DNSServer dns;                             // captive portal: resolves every host to the board
WebServer portal(80);
WebSocketsClient ws;
String ssid, pass, room;
char deviceId[5];                          // last 2 bytes of MAC, e.g. "A4F1" (addressing key)
char deviceName[33];                       // friendly label shown in the app (default = the private name)
char privateRoom[40];                      // unique per-board room, e.g. "fluffy-otter" (private by default)
char privateName[40];                      // same words Title-cased, e.g. "Fluffy Otter" (default name)
uint8_t level = 0;                         // current mist level 0-100
bool localOn = false;                      // mist toggled on by the board button (works offline)
bool btnPrev = false;                      // button edge-detect
unsigned long btnMs = 0;                   // button debounce
bool portalMode = false;                   // true while serving the setup page
bool roomChangePending = false;            // set by an OTA "room" command; reconnect runs in loop()
unsigned long lastTick = 0, lastCmd = 0, lastProbe = 0;
const unsigned long CMD_TIMEOUT = 6000;    // mist off if no command this long

// ---- per-board friendly identity ----------------------------------------
// Two short words picked from the MAC give every board a unique, memorable,
// STABLE name (survives reboot/erase) — e.g. "fluffy-otter". It's the board's
// default room, so a fresh board is private (nobody can drive it without
// knowing its words). Modulo on the list sizes, so the lists can be any length.
static const char* const ADJ[] = {
  "fluffy","misty","dreamy","cozy","breezy","sunny","cloudy","foggy","frosty",
  "snowy","dewy","hazy","balmy","gentle","sleepy","happy","jolly","merry",
  "bouncy","bubbly","perky","zippy","snappy","peppy","sparkly","glowy","shiny",
  "velvety","silky","downy","woolly","mossy","leafy","minty","peachy","honey",
  "sugary","mellow","jazzy","groovy","swirly","wavy","curly","fizzy","plucky",
  "dapper","nifty","spiffy","quirky","lucky","brave","calm","swift","tiny",
  "mighty","noble" };
static const char* const NOUN[] = {
  "otter","panda","koala","llama","alpaca","walrus","badger","beaver","hedgehog",
  "hamster","gecko","newt","puffin","penguin","narwhal","dolphin","seal",
  "manatee","wombat","quokka","lemur","sloth","marmot","ferret","weasel",
  "meerkat","raccoon","fox","lynx","bobcat","ocelot","robin","finch","sparrow",
  "magpie","heron","crane","swan","goose","cygnet","tadpole","minnow","guppy",
  "snail","beetle","cricket","firefly","glowworm","moth","comet","pebble",
  "acorn","pinecone","mushroom","teapot","kettle","lantern","mitten",
  "scooter","kayak","blimp" };

void makeIdentity(const uint8_t* mac) {
  const char* a = ADJ[mac[4] % (sizeof(ADJ) / sizeof(ADJ[0]))];
  const char* n = NOUN[mac[5] % (sizeof(NOUN) / sizeof(NOUN[0]))];
  snprintf(privateRoom, sizeof(privateRoom), "%s-%s", a, n);     // "fluffy-otter"
  snprintf(privateName, sizeof(privateName), "%s %s", a, n);     // "fluffy otter"
  privateName[0] = toupper((unsigned char)privateName[0]);       // -> Title Case
  for (size_t i = 1; privateName[i]; i++)
    if (privateName[i - 1] == ' ') privateName[i] = toupper((unsigned char)privateName[i]);
}

const char* waterName(MistSenseState s) {
  switch (s) {
    case MIST_WATER_OK:          return "ok";
    case MIST_WATER_LOW:         return "low";
    case MIST_DISC_MISSING:      return "no_disc";
    case MIST_DISC_DISCONNECTED: return "disconnected";
    default:                     return "unknown";
  }
}

// percent-encode for the WebSocket query string (a custom name/room may have spaces)
String urlEncode(const char* s) {
  String o; char b[4];
  for (const char* p = s; *p; p++) {
    char c = *p;
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') o += c;
    else { snprintf(b, sizeof(b), "%%%02X", (uint8_t)c); o += b; }
  }
  return o;
}

// minimal JSON-string escape for the few user-entered fields (name / room)
String jsonEsc(const char* s) {
  String o; char u[8];
  for (const char* p = s; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { snprintf(u, sizeof(u), "\\u%04x", c); o += u; }
    else o += c;
  }
  return o;
}

// pull a JSON string value ("key":"value") into out (capped; stops at the closing quote)
bool jsonStr(const char* msg, const char* key, char* out, size_t cap) {
  char pat[20];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char* p = strstr(msg, pat);
  if (!p) { out[0] = 0; return false; }
  p += strlen(pat);
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < cap) out[i++] = *p++;
  out[i] = 0;
  if (*p != '"') { out[0] = 0; return false; }   // ended/clipped before the closing quote -> invalid
  return true;
}

// 0-100 percentage; the library caps real PWM duty at 50%, so 100 = full mist.
void applyLevel(int pct) {
  pct = constrain(pct, 0, 100);
  if (pct == level) return;
  level = pct;
  mist.setLevel((uint16_t)level * 255 / 100);
  Serial.printf("[CMD] mist level -> %u%%\n", level);
}

// ===================== WiFi setup portal (board's own AP) =====================
const char SETUP_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Mist Maker setup</title>
<style>
 body{font-family:system-ui,sans-serif;background:#58c4e0;color:#0c4a61;margin:0;padding:24px;display:flex;justify-content:center}
 .c{background:#edfaff;border-radius:18px;padding:22px;max-width:360px;width:100%;box-shadow:0 10px 30px rgba(12,74,97,.25)}
 h1{font-size:1.2rem;margin:0 0 4px}p{color:#3a6b7e;font-size:.85rem;margin:.3rem 0 1rem}
 label{font-size:.8rem;font-weight:600}
 input{width:100%;padding:11px;margin:4px 0 12px;border:2px solid #6fcfe8;border-radius:10px;font-size:1rem;box-sizing:border-box}
 button{width:100%;padding:13px;border:0;border-radius:12px;font-size:1rem;font-weight:700;color:#0c4a61}
 .save{background:#ffc831}.test{background:#fff;box-shadow:inset 0 0 0 2px #6fcfe8;margin-bottom:14px}.test.on{background:#0c4a61;color:#edfaff}
</style></head><body><div class="c">
<h1>&#9729; Mist Maker setup</h1>
<p>Connect this maker to the workshop WiFi, then Save. Tap Test to check the mist first.</p>
<button class="test" id="t" onclick="test()">&#128168; Test mist (30%)</button>
<form method="POST" action="/save">
 <label>Name this maker (optional)</label>
 <input name="name" id="name" autocomplete="off" maxlength="32" placeholder="e.g. Left, Center, Stage-1">
 <label>Room <span style="font-weight:400;color:#3a6b7e">&middot; same room = shared control</span></label>
 <input name="room" id="room" autocomplete="off" maxlength="32">
 <p style="font-size:.72rem;color:#3a6b7e;margin:-6px 0 12px">Private to this board: <b id="priv">&mdash;</b>
  <a href="#" id="usepriv" style="color:#1d81a6">use it</a>. For a group, type a shared name like <b>workshop</b>.</p>
 <label>WiFi network</label>
 <input name="ssid" list="nets" id="ssid" autocomplete="off" required>
 <datalist id="nets"></datalist>
 <label>Password</label>
 <input name="pass" type="password" autocomplete="off">
 <button class="save">Save &amp; connect</button>
</form>
<p style="font-size:.74rem;color:#3a6b7e;margin:10px 0 0">ID <b id="id">&mdash;</b> &middot; in the app (<b id="host">&mdash;</b>) use the same room to control this maker.</p>
</div><script>
let on=false;
function test(){on=!on;fetch('/test?on='+(on?1:0));let b=document.getElementById('t');
 b.classList.toggle('on',on);b.textContent=on?'■ Stop test':'💨 Test mist (30%)';}
fetch('/info').then(r=>r.json()).then(d=>{document.getElementById('name').value=d.name;
 document.getElementById('room').value=d.room;document.getElementById('id').textContent=d.id;
 document.getElementById('priv').textContent=d.priv;document.getElementById('host').textContent=d.host;}).catch(()=>{});
document.getElementById('usepriv').onclick=function(e){e.preventDefault();
 document.getElementById('room').value=document.getElementById('priv').textContent;};
fetch('/scan').then(r=>r.json()).then(a=>{document.getElementById('nets').innerHTML=
 a.map(s=>'<option value="'+s.replace(/"/g,'')+'">').join('');}).catch(()=>{});
</script></body></html>
)HTML";

void handleScan() {
  int n = WiFi.scanNetworks();
  String j = "[";
  for (int i = 0; i < n; i++) { if (i) j += ","; j += "\"" + jsonEsc(WiFi.SSID(i).c_str()) + "\""; }
  j += "]";
  portal.send(200, "application/json", j);
}
void handleTest() {
  applyLevel(portal.arg("on") == "1" ? 30 : 0);   // local hardware check, no cloud
  portal.send(200, "text/plain", "ok");
}
void handleInfo() {                                // id + name + room + private room + host (prefills page)
  String j = "{\"id\":\"";  j += deviceId;
  j += "\",\"name\":\"";    j += jsonEsc(deviceName);    // user-entered -> escape
  j += "\",\"room\":\"";    j += jsonEsc(room.c_str());  // user-entered -> escape
  j += "\",\"priv\":\"";    j += privateRoom;
  j += "\",\"host\":\"";    j += RELAY_HOST;
  j += "\"}";
  portal.send(200, "application/json", j);
}
void handleSave() {
  ssid = portal.arg("ssid"); pass = portal.arg("pass");
  room = portal.arg("room"); if (room.isEmpty()) room = privateRoom;  // blank = go private
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("name", portal.arg("name"));   // friendly label (blank = keep Mist-XXXX)
  prefs.putString("room", room);                 // which group of makers this belongs to
  applyLevel(0);
  portal.send(200, "text/html",
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<body style='font-family:system-ui;background:#58c4e0;color:#0c4a61;text-align:center;padding:40px'>"
    "<h2>Saved &#9729;</h2><p>Connecting to your WiFi&hellip; the board will restart.</p>");
  Serial.printf("[SETUP] saved WiFi \"%s\" — restarting\n", ssid.c_str());
  delay(900);
  ESP.restart();
}

void startPortal() {
  portalMode = true;
  applyLevel(0);
  WiFi.mode(WIFI_AP);
  char ap[24];
  snprintf(ap, sizeof(ap), "MistMaker-Setup-%s", deviceId);
  WiFi.softAP(ap);
  dns.start(53, "*", WiFi.softAPIP());            // captive portal: resolve every host to us
  portal.on("/", []() { portal.send_P(200, "text/html", SETUP_PAGE); });
  portal.on("/scan", handleScan);
  portal.on("/test", handleTest);
  portal.on("/info", handleInfo);
  portal.on("/save", HTTP_POST, handleSave);
  // Phones probe an "is there internet?" URL; answering any unknown path with
  // the page (200) makes iOS/Android pop the setup page automatically.
  portal.onNotFound([]() { portal.send_P(200, "text/html", SETUP_PAGE); });
  portal.begin();
  Serial.printf("[SETUP] join WiFi \"%s\" — setup page should pop up (else open http://192.168.4.1)\n", ap);
}

// ===================== normal cloud operation =====================
void sendStatus() {
  char buf[160];
  snprintf(buf, sizeof(buf),
    "{\"t\":\"status\",\"level\":%u,\"current_ma\":%.0f,\"water\":\"%s\",\"rssi\":%d}",
    level, mist.readCurrentMa(20), waterName(mist.senseState()), WiFi.RSSI());
  ws.sendTXT(buf);
}

// {"t":"set","target":"all"|"<id>","level":NN} / {"t":"multi","levels":{"<id>":NN}}
void onCommand(const char* msg) {
  if (strstr(msg, "\"t\":\"set\"")) {
    const char* tgt = strstr(msg, "\"target\":\"");
    bool forMe = !tgt;
    if (tgt) {
      tgt += 10;
      const size_t n = strlen(deviceId);
      forMe = !strncmp(tgt, "all\"", 4) || (!strncmp(tgt, deviceId, n) && tgt[n] == '"');
    }
    const char* lv = strstr(msg, "\"level\":");
    if (forMe && lv) {
      int lvl = atoi(lv + 8);
      lastCmd = millis();
      if (lvl != level) localOn = false;             // phone takes over only when it truly changes level
      applyLevel(lvl);                               // (a same-level echo must not strip the button mist)
    }
  } else if (strstr(msg, "\"t\":\"multi\"")) {
    char key[10];
    snprintf(key, sizeof(key), "\"%s\":", deviceId);
    const char* p = strstr(msg, key);
    if (p) {
      int lvl = atoi(p + strlen(key));
      lastCmd = millis();
      if (lvl != level) localOn = false;
      applyLevel(lvl);
    }
  } else if (strstr(msg, "\"t\":\"room\"")) {
    // Move this board to a new room over-the-air (no reboot). Authorized by the
    // board's PRINTED two-word key (its private room name) so it can't be hijacked.
    char cid[8], ckey[40], croom[40];
    jsonStr(msg, "id",   cid,   sizeof(cid));
    jsonStr(msg, "key",  ckey,  sizeof(ckey));
    jsonStr(msg, "room", croom, sizeof(croom));
    if (!strcmp(cid, deviceId) && !strcmp(ckey, privateRoom) && croom[0] && strcmp(croom, room.c_str())) {
      Serial.printf("[ROOM] moving \"%s\" -> \"%s\"\n", room.c_str(), croom);
      room = croom;
      prefs.putString("room", room);
      roomChangePending = true;          // reconnect in loop(), not inside this WS callback
    } else if (!strcmp(cid, deviceId)) {
      Serial.println("[ROOM] move ignored — wrong key or unchanged room");
    }
  }
}

// Board button: toggle a local mist any time (works even with no cloud).
// Last-writer-wins — a phone command takes over, and a press takes over the phone.
void pollButton() {
  bool b = digitalRead(BUTTON) == HIGH;            // active-HIGH (PCB pull-down)
  if (b && !btnPrev && millis() - btnMs > 250) {   // debounced rising edge = a press
    btnMs = millis();
    bool turnOn = (level == 0);                    // toggle by actual mist state (agrees with web/phone)
    localOn = turnOn;                              // remember it's a local, offline-safe mist
    applyLevel(turnOn ? BUTTON_LEVEL : 0);
    lastCmd = millis();                            // exempt from the command watchdog
    Serial.printf("[BUTTON] mist %s\n", turnOn ? "ON" : "OFF");
    if (ws.isConnected()) sendStatus();            // sync to the app the instant it changes
  }
  btnPrev = b;
}

void onWsEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:    Serial.println("[WS] connected — control me from the app");
                              sendStatus(); break;   // sync current state (incl. a button mist) right away
    case WStype_DISCONNECTED: Serial.println("[WS] disconnected — retrying");
                              if (!localOn) applyLevel(0);  // keep a button-toggled mist running offline
                              break;
    case WStype_ERROR:        Serial.println("[WS] error"); break;
    case WStype_TEXT:         onCommand((const char*)payload); break;
    default: break;
  }
}

// (re)point the relay socket at the CURRENT room (startup + OTA room change)
void wsConnect() {
  char path[256];   // room + name up to 32 chars each, ×3 once %-encoded
  snprintf(path, sizeof(path), "/ws?room=%s&role=device&id=%s&name=%s",
           urlEncode(room.c_str()).c_str(), deviceId, urlEncode(deviceName).c_str());
  ws.beginSSL(RELAY_HOST, RELAY_PORT, path);
}
// OTA room move: drop the socket and rejoin the new room's Durable Object — no
// reboot, no WiFi re-entry. Called from loop(), never inside the WS callback.
void reconnectRoom() {
  Serial.printf("[ROOM] reconnecting to \"%s\"\n", room.c_str());
  ws.disconnect();
  wsConnect();
}

void startCloud() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("[WiFi] joining \"%s\"", ssid.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300); Serial.print(".");
    if (millis() - t0 > 20000) {            // bad creds / AP down -> back to setup
      Serial.println(" failed — starting WiFi setup");
      startPortal();
      return;
    }
  }
  Serial.printf(" ok — ip %s, rssi %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  ws.onEvent(onWsEvent);
  ws.setReconnectInterval(3000);
  ws.enableHeartbeat(15000, 3000, 2);
  wsConnect();                              // beginSSL to the current room
  mist.probe();
  Serial.println("[OK] ready — open the app on your phone.");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  mist.disableBattery();   // V0.3 D1 can't tell USB from battery — re-add at V0.4
  mist.begin();
  pinMode(BUTTON, INPUT_PULLDOWN);   // defined idle-LOW so the pin can't float (reliable press + boot)

  uint8_t mac[6];
  WiFi.mode(WIFI_STA);                              // init WiFi first so the MAC reads real (not 0000)
  WiFi.macAddress(mac);
  snprintf(deviceId, sizeof(deviceId), "%02X%02X", mac[4], mac[5]);
  makeIdentity(mac);                                // privateRoom + privateName from the MAC

  prefs.begin("mistwifi", false);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  room = prefs.getString("room", privateRoom);      // private to this board until set to a shared name
  String nm = prefs.getString("name", "");          // optional friendly label (blank = the private name)
  if (nm.length()) nm.toCharArray(deviceName, sizeof(deviceName));
  else { strncpy(deviceName, privateName, sizeof(deviceName) - 1); deviceName[sizeof(deviceName) - 1] = 0; }

  Serial.println();
  Serial.printf("==== %s  (id: %s, room: %s, relay: %s) ====\n", deviceName, deviceId, room.c_str(), RELAY_HOST);
  Serial.printf("[ID] this board is private room \"%s\" — set room to a shared name like \"%s\" for a group\n",
                privateRoom, SHARED_ROOM_EG);

  // No saved WiFi, or button held at boot -> run the setup portal. Otherwise
  // connect (and fall back to the portal if the saved WiFi won't connect).
  if (ssid.isEmpty() || digitalRead(BUTTON) == HIGH) startPortal();
  else startCloud();

  btnPrev = (digitalRead(BUTTON) == HIGH);   // if held to enter setup, don't fire a phantom press
}

void loop() {
  pollButton();   // physical button toggles a local mist in ANY mode (setup AND connected)

  if (portalMode) { dns.processNextRequest(); portal.handleClient(); return; }

  ws.loop();
  if (roomChangePending) { roomChangePending = false; reconnectRoom(); }

  // Fail-safe: a healthy link refreshes lastCmd every ~2 s (phone keepalive);
  // silence past CMD_TIMEOUT means the link dropped — cut phone-driven mist.
  // A button-toggled mist is local control, so it's exempt.
  if (!localOn && level > 0 && millis() - lastCmd > CMD_TIMEOUT) {
    Serial.println("[WATCHDOG] no commands — mist off");
    applyLevel(0);
  }
  // Re-probe water/disc every 20 s using the disc's own current draw. Never keep
  // driving a missing/loose disc: cut the mist (even a button-toggled one) and
  // report it right away; flag low water so the app can warn.
  if (millis() - lastProbe > 20000) {
    lastProbe = millis();
    MistSenseState s = mist.probe();
    if (s == MIST_DISC_MISSING || s == MIST_DISC_DISCONNECTED) {
      if (level > 0 || localOn) {
        Serial.printf("[SAFETY] %s — mist off\n", waterName(s));
        localOn = false; applyLevel(0);
      }
      if (ws.isConnected()) sendStatus();          // tell the app immediately
    } else if (s == MIST_WATER_LOW) {
      Serial.println("[WATER] low — refill soon");
      if (ws.isConnected()) sendStatus();
    }
  }
  // Status to the app + Serial heartbeat, once a second.
  if (millis() - lastTick > 1000) {
    lastTick = millis();
    const float ma = mist.readCurrentMa(20);
    Serial.printf("[STAT] level=%u%%  current=%.0f mA  water=%s  wifi=%s(%d dBm)\n",
                  level, ma, waterName(mist.senseState()),
                  ws.isConnected() ? "relay-ok" : "no-relay", WiFi.RSSI());
    if (ws.isConnected()) sendStatus();
  }
}
