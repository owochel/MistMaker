#include <WiFi.h>
#include <esp_now.h>
#include <MistMaker.h>

// MistMaker pins
const int MIST_OUTPUT_PIN = D1;
const int CURRENT_SENSE_PIN = D2;
const int EN_PIN = D3;
const int LED_PIN = D7;

MistMaker mist(MIST_OUTPUT_PIN, EN_PIN, CURRENT_SENSE_PIN, LED_PIN);

typedef struct __attribute__((packed)) {
  uint8_t level; // 0-255, 0=off
} MistMsg;

void onEspNowRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len < (int)sizeof(MistMsg)) return;
  MistMsg msg;
  memcpy(&msg, incomingData, sizeof(MistMsg));
  mist.applyLevel(msg.level);
}

void setup() {
  Serial.begin(115200);
  mist.begin();

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    return;
  }
  esp_now_register_recv_cb(onEspNowRecv);
  Serial.println("ESP-NOW Control ready. Send {level:0-255} packets.");
}

void loop() {
  static unsigned long last = 0;
  if (millis() - last > 3000) {
    mist.printStatus();
    last = millis();
  }
}


