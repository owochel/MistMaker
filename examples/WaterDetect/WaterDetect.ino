// MistMaker — WaterDetect
// Mist that looks after itself: it knows when the water runs out or the
// disc comes off, stops, and resumes on its own once you fix it.
//
// The board measures the disc's current draw — a disc in water, a dry disc,
// and no disc all read differently, so one sensor tells the whole story.
// This sketch mists 6 s on / 3 s off, checks the disc in every pause, and
// waits (retrying) whenever something is wrong.
//
// Open Serial Monitor at 115200 to watch it think — every pause prints the
// measured current and what the board concluded from it.
//
// Boards: a Seeed XIAO ESP32 in the kit's socket, or an Arduino Uno R3/R4
// or Nano 33 IoT on jumper wires (wiring: examples/JumperWireQuickStart).
// On jumpers, current sense needs two extra wires: A1 -> kit D2 pad, and
// 3.3V -> kit 3V3 pad (in USB mode the sense amp is unpowered without it —
// every reading is 0 mA and this sketch reports "no disc found").

#include <MistMaker.h>

// Battery Kit preset. On the Extension Kit use MistMakerExtensionV01().
MistMaker mist(MistMakerBatteryKitV041());

const unsigned long ON_TIME_MS  = 6000;
const unsigned long OFF_TIME_MS = 3000;

bool misting = false;
unsigned long phaseStart = 0;

const char* stateName(MistSenseState s) {
  switch (s) {
    case MIST_WATER_OK:          return "water OK";
    case MIST_WATER_LOW:         return "water LOW - refill soon";
    case MIST_DISC_MISSING:      return "no disc found";
    case MIST_DISC_DISCONNECTED: return "disc came off";
    default:                     return "checking...";
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  mist.begin();

  Serial.println("WaterDetect - watching the disc...");
  MistSenseState s = mist.probe();
  Serial.print("Starting state: ");
  Serial.println(stateName(s));
  phaseStart = millis();
}

void loop() {
  // Auto-calibration is off for now while its thresholds get re-tuned on
  // production boards; the built-in defaults apply. To experiment anyway,
  // call mist.autoCalibrateSense() with the disc attached and in water.

  const MistSenseState s = mist.senseState();
  const bool okToMist = (s == MIST_WATER_OK || s == MIST_WATER_LOW);
  const unsigned long elapsed = millis() - phaseStart;

  if (okToMist) {
    if (misting && elapsed >= ON_TIME_MS) {
      mist.turnOff();
      misting = false;
      phaseStart = millis();
      // Check the disc during the pause, so the rhythm never stutters.
      MistSenseState now = mist.probe();
      Serial.print(mist.lastProbeMa(), 1);
      Serial.print(" mA -> ");
      Serial.println(stateName(now));
    } else if (!misting && elapsed >= OFF_TIME_MS) {
      mist.turnOn();
      misting = true;
      phaseStart = millis();
    }
  } else {
    // Something's wrong: stay off, re-check every 5 s. Refill the water or
    // reattach the disc and it resumes by itself.
    if (misting) { mist.turnOff(); misting = false; }
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry > 5000) {
      lastRetry = millis();
      MistSenseState now = mist.probe();
      Serial.print("waiting: ");
      Serial.println(stateName(now));
      if (now == MIST_WATER_OK) {
        Serial.println("All good again - resuming.");
        phaseStart = millis();
      }
    }
  }
  delay(10);
}
