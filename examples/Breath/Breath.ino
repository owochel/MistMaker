// MistMaker — Breath
// Mist that breathes: in... hold... out... rest. Like an LED breathing,
// but with mist.
//
// setLevel(0..255) dims mist like an LED. The breath starts at MIN_LEVEL
// instead of 0 because mist only becomes visible around there — starting
// higher keeps the whole breath visible.
//
// Boards: a Seeed XIAO ESP32 in the kit's socket, or an Arduino Uno R3/R4
// or Nano 33 IoT on jumper wires (wiring: examples/JumperWireQuickStart).

#include <MistMaker.h>

// Battery Kit preset. On the Extension Kit use MistMakerExtensionV01().
MistMaker mist(MistMakerBatteryKitV041());

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
