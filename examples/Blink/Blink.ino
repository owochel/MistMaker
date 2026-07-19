// MistMaker — Blink
// Mist on, mist off. The "hello world" of mist.
//
// Put the disc in water, plug in USB, upload: a 6-second puff of mist every
// 9 seconds. The status LED follows the mist. Change the delays for your
// own rhythm.
//
// Works on the Battery Kit and the Extension Kit as wired below.
// Board: Seeed XIAO ESP32-C6 — pick "XIAO_ESP32C6" in Tools > Board.

#include <MistMaker.h>

// MistMaker pins
const int MIST_OUTPUT_PIN = D0;
const int CURRENT_SENSE_PIN = D2;
const int EN_PIN = D3;
const int LED_PIN = D7;

MistMaker mist(MIST_OUTPUT_PIN, EN_PIN, CURRENT_SENSE_PIN, LED_PIN);

void setup() {
  mist.begin();
}

void loop() {
  mist.turnOn();     // mist + LED on
  delay(6000);
  mist.turnOff();    // mist + LED off
  delay(3000);
}
