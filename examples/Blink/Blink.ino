// MistMaker — Blink
// Mist on, mist off. The "hello world" of mist.
//
// Put the disc in water, plug in USB, upload: a 6-second puff of mist every
// 9 seconds. The status LED follows the mist. Change the delays for your
// own rhythm.
//
// Boards: a Seeed XIAO ESP32 in the kit's socket, or an Arduino Uno R3/R4
// or Nano 33 IoT on jumper wires (wiring: examples/JumperWireQuickStart).

#include <MistMaker.h>

// Battery Kit preset. On the Extension Kit use MistMakerExtensionV01().
MistMaker mist(MistMakerBatteryKitV041());

void setup() {
  mist.begin();
}

void loop() {
  mist.turnOn();     // mist + LED on
  delay(6000);
  mist.turnOff();    // mist + LED off
  delay(3000);
}
