// WaterDetect — piezo disc + water-level detection through current sensing.
//
// The mist boards measure piezo current through a shunt + INA180A3 amplifier.
// A missing disc, a dry disc, and a disc in water each draw a distinctly
// different current at a given PWM duty, so one ADC pin gives you:
//   * disc presence detection (is a piezo attached at all?)
//   * water-level detection  (is there still water to mist?)
//   * disconnect detection   (did the disc fall off mid-run?)
//
// Thresholds: the library ships with bench-measured defaults
// (disc present > 10 mA at duty 10, water low < 110 mA at duty 64).
// Send 'c' over Serial to auto-calibrate against YOUR disc + water instead —
// then copy the printed values into setSenseThresholds() below.
//
// Behavior: mists 6 s ON / 3 s OFF while water is OK. Probes during every
// OFF window. Stops (and blinks the status LED if your board has one) when
// the disc is missing or the water runs out; refill/reattach and it resumes.
//
// Board: Seeed XIAO ESP32-C6 (select XIAO_ESP32C6 in Tools > Board)
// Library: MistMaker >= 1.1.0

#include <MistMaker.h>

// ---- Select your board (uncomment exactly ONE) ----
MistMaker mist(MistMakerBatteryKitV03());
// MistMaker mist(MistMakerExtensionV01());
// MistMaker mist(MistMakerBlockKitV01());
// MistMaker mist(MistMakerLegacyV1());

// Optional: after running auto-calibration once, hard-code your thresholds
// here so every boot starts calibrated (values in mA):
// (disc present, water low, disc disconnected)
// mist.setSenseThresholds(10.0, 110.0, 70.0);

const unsigned long ON_TIME_MS  = 6000;
const unsigned long OFF_TIME_MS = 3000;

bool misting = false;
unsigned long phaseStart = 0;

const char* stateName(MistSenseState s) {
  switch (s) {
    case MIST_WATER_OK:          return "WATER_OK";
    case MIST_WATER_LOW:         return "WATER_LOW";
    case MIST_DISC_MISSING:      return "DISC_MISSING";
    case MIST_DISC_DISCONNECTED: return "DISC_DISCONNECTED";
    default:                     return "UNKNOWN";
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  mist.begin();

  Serial.println("WaterDetect: 'c' = auto-calibrate (disc attached, in water)");
  Serial.println("Probing for disc + water...");
  MistSenseState s = mist.probe();
  Serial.print("Initial state: ");
  Serial.println(stateName(s));
  phaseStart = millis();
}

void loop() {
  // Serial command: 'c' runs auto-calibration.
  if (Serial.available() && Serial.read() == 'c') {
    mist.turnOff();
    misting = false;
    mist.autoCalibrateSense();
    phaseStart = millis();
  }

  const MistSenseState s = mist.senseState();
  const bool okToMist = (s == MIST_WATER_OK || s == MIST_WATER_LOW);
  const unsigned long elapsed = millis() - phaseStart;

  if (okToMist) {
    if (misting && elapsed >= ON_TIME_MS) {
      mist.turnOff();
      misting = false;
      phaseStart = millis();
      // Probe during the OFF window so the user never sees the hiccup.
      MistSenseState ns = mist.probe();
      Serial.print("probe: ");
      Serial.print(mist.lastProbeMa(), 1);
      Serial.print(" mA -> ");
      Serial.println(stateName(ns));
    } else if (!misting && elapsed >= OFF_TIME_MS) {
      mist.turnOn();
      misting = true;
      phaseStart = millis();
    }
  } else {
    // Disc missing / disconnected / unknown: stay off, re-probe every 5 s
    // so reattaching the disc or refilling water auto-resumes.
    if (misting) { mist.turnOff(); misting = false; }
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry > 5000) {
      lastRetry = millis();
      MistSenseState ns = mist.probe();
      Serial.print("waiting (");
      Serial.print(stateName(ns));
      Serial.print(") probe ");
      Serial.print(mist.lastProbeMa(), 1);
      Serial.println(" mA");
      if (ns == MIST_WATER_OK) {
        Serial.println("Recovered! Resuming mist cycle.");
        phaseStart = millis();
      }
    }
  }
  delay(10);
}
