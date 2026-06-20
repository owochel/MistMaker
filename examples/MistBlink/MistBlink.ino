// MistBlink — the "hello world" of mist.
//
// Turns the mist on and off on a fixed cycle (6 s ON / 3 s OFF, the duty
// cycle we recommend so the disc and water column get a rest between bursts).
//
// Works on every official Programmable Mist Maker board — uncomment the
// preset that matches your PCB below.
//
// Board: Seeed XIAO ESP32-C6 (select XIAO_ESP32C6 in Tools > Board)
// Library: MistMaker >= 1.1.0

#include <MistMaker.h>

// ---- Select your board (uncomment exactly ONE) ----
MistMaker mist(MistMakerBatteryKitV03());
// MistMaker mist(MistMakerExtensionV01());
// MistMaker mist(MistMakerBlockKitV01());
// MistMaker mist(MistMakerLegacyV1());

const unsigned long ON_TIME_MS  = 6000;
const unsigned long OFF_TIME_MS = 3000;

void setup() {
  Serial.begin(115200);
  delay(1000);
  mist.disableBattery();   // V0.3 D1 can't tell USB from battery — re-add at V0.4
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
