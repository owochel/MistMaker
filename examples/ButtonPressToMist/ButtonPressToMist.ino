// MistMaker — Button
// Hold the board button to mist; release to stop. Uses the library's
// buttonPressed() helper (active-HIGH, PCB pull-down — no debounce).
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

MistMaker mist(MIST_OUTPUT_PIN, EN_PIN, CURRENT_SENSE_PIN, LED_PIN, BUTTON_PIN);

void setup() {
  mist.begin();
}

void loop() {
  if (mist.buttonPressed()) {
    mist.setLevel(255);
  } else {
    mist.setLevel(0);
  }
}
