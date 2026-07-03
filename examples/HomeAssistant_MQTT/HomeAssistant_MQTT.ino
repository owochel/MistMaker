// HomeAssistant_MQTT — make the mist maker a native Home Assistant device.
//
// Uses MQTT Discovery: flash, power up, and the device appears in HA
// automatically (Settings > Devices > MQTT) as:
//   * a dimmable "light" entity controlling the mist (HA's light card gives
//     us an on/off toggle + brightness slider for free)
//   * a water/disc status sensor (from current sensing)
//
// Battery: Battery Kit V0.4 routes the TPS2116 mux STATUS pin to D8, so the
// library can tell USB from the cell. This example publishes a battery %
// sensor and deep-sleeps on a critically low cell — batteryState() reads
// CHARGING on USB, so it never sleeps while plugged in. On V0.3 (no ST pin)
// switch the preset below and uncomment mist.disableBattery() in setup().
//
// Requirements:
//   * MQTT broker (the standard Mosquitto add-on) + MQTT integration in HA
//   * Arduino library: PubSubClient (Library Manager)
//   * MistMaker >= 2.0.0
//
// Prefer YAML/no-code? See the ESPHome config in the main repo:
// Programmable-Mist-Maker/firmware-examples/home-assistant/esphome-mistmaker.yaml
//
// Board: Seeed XIAO ESP32-C6 (select XIAO_ESP32C6 in Tools > Board)

#include <MistMaker.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_sleep.h>          // deep sleep on a critically low cell

// ---- Select your board (uncomment exactly ONE) ----
MistMaker mist(MistMakerBatteryKitV04());   // V0.4 board: ST on D8 gates battery vs USB
// MistMaker mist(MistMakerBatteryKitV03()); // V0.3 board: use this + uncomment disableBattery() (D8 floats on V0.3)
// MistMaker mist(MistMakerExtensionV01());
// MistMaker mist(MistMakerBlockKitV01());

// ---- Your network + broker ----
const char* WIFI_SSID  = "your-wifi";
const char* WIFI_PASS  = "your-password";
const char* MQTT_HOST  = "homeassistant.local"; // or broker IP
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER  = "mqtt-user";           // "" if anonymous
const char* MQTT_PASS  = "mqtt-pass";

// Unique per device — change if you run several mist makers.
const char* DEV_ID = "mistmaker01";

WiFiClient wifi;
PubSubClient mqtt(wifi);

char topicCmd[64], topicState[64], topicAvail[64], topicSensors[64];
unsigned long lastSensorPub = 0;
unsigned long lastBattMs = 0;
unsigned long lastMqttTry = 0;

const char* waterName(MistSenseState s) {
  switch (s) {
    case MIST_WATER_OK:          return "ok";
    case MIST_WATER_LOW:         return "low";
    case MIST_DISC_MISSING:      return "no_disc";
    case MIST_DISC_DISCONNECTED: return "disconnected";
    default:                     return "unknown";
  }
}

void publishState() {
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"state\":\"%s\",\"brightness\":%u}",
           mist.isOn() ? "ON" : "OFF", mist.getLevel());
  mqtt.publish(topicState, buf, true);
}

void publishSensors() {
  char buf[128];
  // battery% is voltage-derived; on USB it tracks the charger, not true SoC —
  // the "charging" flag lets HA automations ignore the number while plugged in.
  const bool charging = (mist.batteryState() == MIST_BATT_CHARGING);
  snprintf(buf, sizeof(buf),
           "{\"water\":\"%s\",\"battery\":%u,\"charging\":%s}",
           waterName(mist.senseState()), mist.batteryPercent(),
           charging ? "true" : "false");
  mqtt.publish(topicSensors, buf, true);
}

// HA MQTT Discovery: one retained config message per entity.
void publishDiscovery() {
  char topic[128], payload[512];
  const char* dev =
      "\"dev\":{\"ids\":[\"%s\"],\"name\":\"Mist Maker\","
      "\"mf\":\"Power Plane\",\"mdl\":\"Programmable Mist Maker\"}";
  char devBuf[160];
  snprintf(devBuf, sizeof(devBuf), dev, DEV_ID);

  // Light (mist with brightness)
  snprintf(topic, sizeof(topic), "homeassistant/light/%s/config", DEV_ID);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"Mist\",\"uniq_id\":\"%s_mist\",\"schema\":\"json\","
    "\"cmd_t\":\"%s\",\"stat_t\":\"%s\",\"avty_t\":\"%s\","
    "\"brightness\":true,\"bri_scl\":255,%s}",
    DEV_ID, topicCmd, topicState, topicAvail, devBuf);
  mqtt.publish(topic, payload, true);

  // Water status sensor
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_water/config", DEV_ID);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"Mist Maker Water\",\"uniq_id\":\"%s_water\","
    "\"stat_t\":\"%s\",\"avty_t\":\"%s\","
    "\"val_tpl\":\"{{ value_json.water }}\",%s}",
    DEV_ID, topicSensors, topicAvail, devBuf);
  mqtt.publish(topic, payload, true);

  // Battery % sensor (Battery Kit V0.4 with a cell). device_class battery gives
  // HA the battery icon; the value rides on the same sensors topic.
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_battery/config", DEV_ID);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"Mist Maker Battery\",\"uniq_id\":\"%s_batt\","
    "\"dev_cla\":\"battery\",\"unit_of_meas\":\"%%\","
    "\"stat_t\":\"%s\",\"avty_t\":\"%s\","
    "\"val_tpl\":\"{{ value_json.battery }}\",%s}",
    DEV_ID, topicSensors, topicAvail, devBuf);
  mqtt.publish(topic, payload, true);
}

void onMqtt(char* topic, byte* payload, unsigned int len) {
  // Expected JSON from HA: {"state":"ON","brightness":128} (brightness optional)
  char msg[128];
  if (len >= sizeof(msg)) len = sizeof(msg) - 1;
  memcpy(msg, payload, len);
  msg[len] = '\0';

  if (strstr(msg, "\"OFF\"")) {
    mist.turnOff();
  } else {
    const char* b = strstr(msg, "\"brightness\":");
    if (b) mist.setLevel(constrain(atoi(b + 13), 0, 255));
    else   mist.turnOn();
  }
  publishState();
}

// One (re)connect attempt + republish. Non-blocking by design: loop() retries
// on a backoff, so the low-battery watchdog keeps running when WiFi or the
// broker is down (a blocking connect loop would strand a weak cell).
void connectMqtt() {
  if (mqtt.connected()) return;
  // LWT marks the device unavailable in HA if we drop off the network.
  if (mqtt.connect(DEV_ID, MQTT_USER, MQTT_PASS,
                   topicAvail, 0, true, "offline")) {
    mqtt.publish(topicAvail, "online", true);
    mqtt.subscribe(topicCmd);
    publishDiscovery();
    publishState();
    publishSensors();
    Serial.println("[MQTT] connected + discovery published");
  } else {
    Serial.print("[MQTT] connect failed rc=");
    Serial.println(mqtt.state());
  }
}

// Low-battery guard. batteryState() is ST-gated: it returns MIST_BATT_CHARGING
// on USB, so this only fires on a genuinely dying cell — never while plugged in
// to reflash. On CRITICAL: mark offline, mist off, radio off, deep-sleep to
// protect the LiPo (recharge + reset/power-cycle wakes it).
void checkBattery() {
  // Require TWO consecutive CRITICAL reads (~10 s) before the irreversible
  // deep-sleep. A single sample can lie: the boost sags BATT+ under mist load,
  // and on a V0.3 board the sense pin floats — neither should strand the board.
  static uint8_t critStreak = 0;
  if (mist.batteryState() != MIST_BATT_CRITICAL) { critStreak = 0; return; }
  if (++critStreak < 2) return;
  Serial.println("[BATTERY] Critical on cell - graceful shutdown.");
  mqtt.publish(topicAvail, "offline", true);
  mqtt.loop();
  delay(150);              // let the retained 'offline' flush before the radio dies
  mist.shutdown();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  // On Battery Kit V0.3 (no ST pin) uncomment this so the unreliable D1 reading
  // can't trigger a false low-battery shutdown:
  // mist.disableBattery();
  mist.begin();

  snprintf(topicCmd,     sizeof(topicCmd),     "mistmaker/%s/set",     DEV_ID);
  snprintf(topicState,   sizeof(topicState),   "mistmaker/%s/state",   DEV_ID);
  snprintf(topicAvail,   sizeof(topicAvail),   "mistmaker/%s/avail",   DEV_ID);
  snprintf(topicSensors, sizeof(topicSensors), "mistmaker/%s/sensors", DEV_ID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  // Bounded wait: don't block boot forever on a dead AP — the ESP32 keeps
  // auto-reconnecting, and loop()'s battery watchdog must stay reachable.
  const uint32_t wifiT0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiT0 < 20000) {
    delay(300); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" connected: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println(" not up yet - continuing; will retry in the background.");
  }

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(640); // discovery payloads are bigger than the default
  mqtt.setCallback(onMqtt);
  connectMqtt();

  mist.probe();
}

void loop() {
  // Retry MQTT on a 2 s backoff (not every iteration) so a down broker can't
  // starve the rest of loop(), including the low-battery watchdog below.
  if (!mqtt.connected() && millis() - lastMqttTry > 2000) {
    lastMqttTry = millis();
    connectMqtt();
  }
  mqtt.loop();

  // Publish sensors (and re-probe water) every 60 s.
  if (millis() - lastSensorPub > 60000) {
    lastSensorPub = millis();
    mist.probe();
    publishSensors();
  }

  // Low-battery guard every 5 s (no-op on USB / boards without a cell).
  if (millis() - lastBattMs > 5000) {
    lastBattMs = millis();
    checkBattery();
  }
}
