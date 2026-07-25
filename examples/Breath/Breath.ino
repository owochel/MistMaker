// MistMaker — Breath
// Mist that breathes: in... hold... out... rest. Like an LED breathing,
// but with mist.
//
// setLevel(0..255) dims mist like an LED. The breath starts at MIN_LEVEL
// instead of 0 because mist only becomes visible around there — starting
// higher keeps the whole breath visible.
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

// The feel of the breath — tweak to taste
const int MIN_LEVEL    = 60;    // where mist becomes visible
const int STEP_MS      = 20;    // smaller = faster breathing
const int HOLD_MS      = 400;   // pause at full breath
const int REST_MS      = 900;   // pause between breaths

void setup() {
  mist.begin();
}

void loop() {
  for (int level = MIN_LEVEL; level <= 255; level++) {   // breathe in
    mist.setLevel(level);
    delay(STEP_MS);
  }
  delay(HOLD_MS);

  for (int level = 255; level >= MIN_LEVEL; level--) {   // breathe out
    mist.setLevel(level);
    delay(STEP_MS);
  }
  mist.setLevel(0);                                      // rest
  delay(REST_MS);
}
