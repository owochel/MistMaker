// MistMaker — Button On/Off
// Press once to turn the mist on; press again to turn it off. The library
// returns the raw button level, so this sketch detects each press and debounces
// it before calling toggle().
//
// Works on the Battery Kit as wired below. Extension Kit has no button —
// pass your own pin as the 5th constructor argument, or call setButtonPin().
// Board: Seeed XIAO ESP32-C6 — pick "XIAO_ESP32C6" in Tools > Board.

#include <MistMaker.h>

// MistMaker pins
const int MIST_OUTPUT_PIN   = D0;
const int CURRENT_SENSE_PIN = D2;
const int EN_PIN            = D3;
const int LED_PIN           = D7;
const int BUTTON_PIN        = D6; // Built-in button on the Battery Kit

const unsigned long DEBOUNCE_MS = 30;

MistMaker mist(MIST_OUTPUT_PIN, EN_PIN, CURRENT_SENSE_PIN, LED_PIN, BUTTON_PIN);

bool lastRawButton = false;
bool stableButton  = false;
unsigned long lastButtonChangeMs = 0;

void setup() {
  mist.begin();
}

void loop() {
  const bool rawButton = mist.buttonPressed();

  if (rawButton != lastRawButton) {
    lastRawButton = rawButton;
    lastButtonChangeMs = millis();
  }

  if (millis() - lastButtonChangeMs >= DEBOUNCE_MS &&
      stableButton != rawButton) {
    stableButton = rawButton;

    // Toggle once on the press edge, not continuously while held.
    if (stableButton) {
      mist.toggle();
    }
  }
}
