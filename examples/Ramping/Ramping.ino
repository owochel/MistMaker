// Test_MistMaker_Only.ino
// Verify mist maker works before adding ESP-NOW

#include <Arduino.h>
#include <MistMaker.h>

const int MIST_OUTPUT_PIN = D1;
const int EN_PIN = D3;
const int CURRENT_SENSE_PIN = D2;
const int LED_PIN = D7;


MistMaker mist(MIST_OUTPUT_PIN, EN_PIN, CURRENT_SENSE_PIN, LED_PIN);

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("MistMaker test starting...");
  
  mist.begin();
  Serial.println("MistMaker initialized");
}

void loop() {
  Serial.println("Ramping up...");
  for (int i = 0; i <= 255; i += 25) {
    mist.applyLevel(i);
    Serial.printf("Level: %d\n", i);
    delay(500);
  }
  
  Serial.println("Ramping down...");
  for (int i = 255; i >= 0; i -= 25) {
    mist.applyLevel(i);
    Serial.printf("Level: %d\n", i);
    delay(500);
  }
  
  delay(1000);
}