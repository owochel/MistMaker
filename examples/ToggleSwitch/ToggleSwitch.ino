// MistMaker — Toggle Switch
// Mist follows the switch *level*: closed = mist, open = no mist. That is the
// natural pattern for a latching toggle switch (SPST) — flip it on and the
// mist runs until you flip it off. No debounce needed: a bit of contact
// bounce just re-asserts the same level a moment later.
//
// On a momentary push button (including the Battery Kit's on-board one) the
// same sketch gives you hold-to-mist. For press-on / press-off from a
// momentary button, see the PushButton example instead.
//
// Wiring: switch between the button pin and 3V3 — the kits have a pull-down
// on the PCB, so buttonPressed() is active-HIGH.
// Works on the Battery Kit (its preset includes the on-board button).
// Extension Kit has no button — wire one and call setButtonPin().
// Boards: a Seeed XIAO ESP32 in the kit's socket, or an Arduino Uno R3/R4
// or Nano 33 IoT on jumper wires (wiring: examples/JumperWireQuickStart).

#include <MistMaker.h>

MistMaker mist(MistMakerBatteryKitV041());

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
