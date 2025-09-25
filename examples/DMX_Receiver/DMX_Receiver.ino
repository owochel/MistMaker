#include <MistMaker.h>

// Requires an ESP32 DMX library (e.g. "ESP32DMX" by Claude Heintz)
// Install via Library Manager, then include its header:
#include <ESP32DMX.h>

// Pins for your board/RS485 transceiver
const int DMX_RX_PIN = 16;  // RO from MAX485 -> ESP32 RX
const int DMX_TX_PIN = 17;  // DI (unused for receive-only)
const int DMX_DE_PIN = 21;  // DE/RE control pin

// MistMaker pins
const int MIST_OUTPUT_PIN = D1;
const int CURRENT_SENSE_PIN = D2;
const int EN_PIN = D3;
const int LED_PIN = D7;

// DMX configuration
const uint16_t DMX_START_ADDRESS = 1; // first channel for this node

ESP32DMX dmx;
MistMaker mist(MIST_OUTPUT_PIN, EN_PIN, CURRENT_SENSE_PIN, LED_PIN);

void setup() {
  Serial.begin(115200);
  mist.begin();

  dmx.setPins(DMX_TX_PIN, DMX_RX_PIN, DMX_DE_PIN);
  dmx.init();
  dmx.setDirectionPin(DMX_DE_PIN);
  dmx.setRX();

  Serial.println("DMX Receiver → MistMaker level on CH1");
}

void loop() {
  dmx.readDMX();
  uint8_t level = dmx.read(DMX_START_ADDRESS);
  mist.applyLevel(level);

  static unsigned long last = 0;
  if (millis() - last > 2000) {
    Serial.print("DMX level: ");
    Serial.println(level);
    last = millis();
  }
}


