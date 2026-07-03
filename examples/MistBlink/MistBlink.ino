// MistBlink — the "hello world" of mist.
//
// Turns the mist on and off on a fixed cycle (6 s ON / 3 s OFF, the duty
// cycle we recommend so the disc and water column get a rest between bursts).
//
// Works on every official Programmable Mist Maker board — uncomment the
// preset that matches your PCB below.
//
// Board: Seeed XIAO ESP32-C6 (select XIAO_ESP32C6 in Tools > Board)
// Library: MistMaker >= 2.0.0

#include <MistMaker.h>

// ---- Select your board (uncomment exactly ONE) ----
MistMaker mist(MistMakerBatteryKitV04());   // V0.4 board: ST on D8 gates battery vs USB
// MistMaker mist(MistMakerBatteryKitV03()); // V0.3 board: use this + uncomment disableBattery() (D8 floats on V0.3)
// MistMaker mist(MistMakerExtensionV01());
// MistMaker mist(MistMakerBlockKitV01());
// MistMaker mist(MistMakerLegacyV1());

const unsigned long ON_TIME_MS  = 6000;
const unsigned long OFF_TIME_MS = 3000;

void setup() {
  Serial.begin(115200);
  delay(1000);
  // On Battery Kit V0.3 (no ST pin) uncomment so the unreliable D1 reading
  // can't cause a false low-battery shutdown:
  // mist.disableBattery();
  mist.begin();
  Serial.println("MistBlink: 6 s ON / 3 s OFF");
}

void loop() {
  mist.turnOn();
  mist.printStatus();
  delay(ON_TIME_MS);

  mist.turnOff();
  mist.printStatus();
  delay(OFF_TIME_MS);
}
