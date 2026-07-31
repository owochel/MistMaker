// MistMaker — Button
// Hold the board button to mist; release to stop. Uses the library's
// buttonPressed() helper (active-HIGH, PCB pull-down — no debounce).
//
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
