// HomeAssistant_MQTT — make the mist maker a native Home Assistant device.
//
// Uses MQTT Discovery: flash, power up, and the device appears in HA
// automatically (Settings > Devices > MQTT) as:
//   * a dimmable "light" entity controlling the mist (HA's light card gives
//     us an on/off toggle + brightness slider for free)
//   * a water/disc status sensor (from current sensing)
//
// NOTE: battery sensing is intentionally OFF on this branch — Battery Kit V0.3
// can't tell USB-C power from a real cell on D1, so the reading is unreliable.
// TODO(V0.4): re-add the battery % sensor + low-battery deep-sleep once the
// board has a real USB-present sense pin.
//
// Requirements:
//   * MQTT broker (the standard Mosquitto add-on) + MQTT integration in HA
//   * Arduino library: PubSubClient (Library Manager)
//   * MistMaker >= 1.1.0
//
// Prefer YAML/no-code? See the ESPHome config in the main repo:
// Programmable-Mist-Maker/firmware-examples/home-assistant/esphome-mistmaker.yaml
//
// Board: Seeed XIAO ESP32-C6 (select XIAO_ESP32C6 in Tools > Board)

#include <MistMaker.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ---- Select your board (uncomment exactly ONE) ----
MistMaker mist(MistMakerBatteryKitV03());
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
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"water\":\"%s\"}", waterName(mist.senseState()));
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

void connectMqtt() {
  while (!mqtt.connected()) {
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
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  mist.disableBattery();   // V0.3: D1 can't tell USB from battery (see note up top)
  mist.begin();

  snprintf(topicCmd,     sizeof(topicCmd),     "mistmaker/%s/set",     DEV_ID);
  snprintf(topicState,   sizeof(topicState),   "mistmaker/%s/state",   DEV_ID);
  snprintf(topicAvail,   sizeof(topicAvail),   "mistmaker/%s/avail",   DEV_ID);
  snprintf(topicSensors, sizeof(topicSensors), "mistmaker/%s/sensors", DEV_ID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.print(" connected: ");
  Serial.println(WiFi.localIP());

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(640); // discovery payloads are bigger than the default
  mqtt.setCallback(onMqtt);
  connectMqtt();

  mist.probe();
}

void loop() {
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();

  // Publish sensors (and re-probe water) every 60 s.
  if (millis() - lastSensorPub > 60000) {
    lastSensorPub = millis();
    mist.probe();
    publishSensors();
  }
}
