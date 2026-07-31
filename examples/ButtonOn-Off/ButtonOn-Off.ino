// MistMaker — Button On/Off
// Press once to turn the mist on; press again to turn it off. The library
// returns the raw button level, so this sketch detects each press and debounces
// it before calling toggle().
//
// Works on the Battery Kit (its preset includes the on-board button).
// Extension Kit has no button — wire one and call setButtonPin().
// Boards: a Seeed XIAO ESP32 in the kit's socket, or an Arduino Uno R3/R4
// or Nano 33 IoT on jumper wires (wiring: examples/JumperWireQuickStart).

#include <MistMaker.h>

const unsigned long DEBOUNCE_MS = 30;

MistMaker mist(MistMakerBatteryKitV041());

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
