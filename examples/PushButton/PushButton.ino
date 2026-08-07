// MistMaker — Push Button
// Press once to turn the mist on; press again to turn it off. This needs a
// *momentary* push button — the kind that springs back, like the Battery Kit's
// on-board one. The sketch watches for the press *edge* rather than the level,
// so holding the button doesn't re-toggle.
//
// buttonPressed() returns the raw level, so the edge has to be debounced here:
// contact bounce would otherwise read as several presses and flip the mist
// back and forth on a single push.
//
// A latching toggle switch won't work well with this sketch — it would toggle
// once when you flip it on and do nothing when you flip it off. Use the
// ToggleSwitch example for that.
//
// Wiring: button between the button pin and 3V3 — the kits have a pull-down
// on the PCB, so buttonPressed() is active-HIGH.
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
