#ifndef MIST_MAKER_H
#define MIST_MAKER_H

#include <Arduino.h>

// ===========================================================================
// MistMaker — Arduino library for the Programmable Mist Maker (OSHWA US002742)
//
// v1.1 adds (on top of shuang cai's v1.0 API, which is fully preserved):
//   * Board pin presets for the official PCB variants
//   * Mist dimming via setLevel(0..255)
//   * Current sensing in real units (mA) with auto or manual calibration
//   * Piezo-disc presence + water-level detection (probe + classifier)
//   * Battery voltage monitoring (resistor divider) + low-battery helpers
//
// Hardware background: the boards sense piezo current through a shunt +
// INA180A3 current-sense amplifier into an ADC pin. A dry disc, a missing
// disc, and a disc in water each draw distinctly different current at a
// given PWM duty — so one ADC pin gives you disc detection AND a water-level
// sensor for free. Default thresholds come from bench measurements on the
// Block Kit V0.1 (XIAO ESP32-C6, INA180A3, 30 mOhm shunt, 2026-05).
// ===========================================================================

// ---------------------------------------------------------------------------
// Pin bundle. Use -1 for anything your board doesn't have.
// ---------------------------------------------------------------------------
struct MistMakerPins {
  int8_t mist;     // PWM output to MOSFET gate (required)
  int8_t boostEn;  // TPS61023 boost enable, HIGH = 5V rail on (-1 = none/always on)
  int8_t sense;    // current-sense ADC input (-1 = none)
  int8_t led;      // status LED (-1 = none)
  int8_t button;   // user button (-1 = none) — read it yourself, kept for reference
  int8_t battery;  // battery-voltage ADC via divider (-1 = none)
};

// ---------------------------------------------------------------------------
// Board presets — match the silkscreen / KiCad nets of each official PCB.
// XIAO D0..D10 constants exist on all Seeed XIAO ESP32 cores.
// ---------------------------------------------------------------------------
#if defined(ARDUINO_XIAO_ESP32C6) || defined(ARDUINO_XIAO_ESP32S3) || \
    defined(ARDUINO_XIAO_ESP32S3_PLUS) || defined(ARDUINO_XIAO_ESP32C3)

// Mist Maker Extension (Seeed-Expansion) V0.1 — XIAO add-on, USB powered,
// no battery, no boost-enable (piezo rail always on).
inline MistMakerPins MistMakerExtensionV01() {
  return MistMakerPins{ D0, -1, D2, -1, -1, -1 };
}

// Mist Maker Battery Kit V0.3 — LiPo + USB-C, battery divider on D1.
inline MistMakerPins MistMakerBatteryKitV03() {
  return MistMakerPins{ D0, D3, D2, D7, D6, D1 };
}

// Block Kit V0.1 — OHS 2026 demo board (reed on D10, IS31FL3731 LEDs on I2C).
inline MistMakerPins MistMakerBlockKitV01() {
  return MistMakerPins{ D0, D3, D2, D7, D6, -1 };
}

// Legacy V1.x single PCB (OHS 2025 workshop board).
inline MistMakerPins MistMakerLegacyV1() {
  return MistMakerPins{ D1, D3, D2, D7, D6, -1 };
}
#endif

// ---------------------------------------------------------------------------
// Sense classifier results
// ---------------------------------------------------------------------------
enum MistSenseState : uint8_t {
  MIST_SENSE_UNKNOWN = 0,   // no probe run yet (or no sense pin)
  MIST_DISC_MISSING,        // no piezo disc detected
  MIST_WATER_OK,            // disc present, enough water
  MIST_WATER_LOW,           // disc present, water low / dry — refill soon
  MIST_DISC_DISCONNECTED,   // disc was there, current collapsed mid-run
};

enum MistBatteryState : uint8_t {
  MIST_BATT_UNKNOWN = 0,    // no battery pin configured
  MIST_BATT_OK,
  MIST_BATT_LOW,            // below low threshold — warn the user
  MIST_BATT_CRITICAL,       // below cutoff — shut down gracefully NOW
};

class MistMaker {
public:
  // --- v1.0 constructor (unchanged, backward compatible) ---
  MistMaker(int mistPin, int enPin, int sensePin, int ledPin,
            int pwmFreq = 108700, int pwmRes = 8, int duty = 127);

  // --- v1.1 constructor from a pin preset ---
  MistMaker(const MistMakerPins &pins,
            int pwmFreq = 108700, int pwmRes = 8, int duty = 127);

  void begin();

  // ------------------- basic control (v1.0 API) -------------------
  void turnOn();              // full power (dutyMax)
  void turnOff();
  void toggle();
  bool isOn();
  void printStatus();
  float readCurrentVoltage(); // raw sense-pin voltage (kept for compat)

  // ------------------- dimming -------------------
  // level 0..255 maps linearly onto 0..dutyMax. 0 = off.
  void setLevel(uint8_t level);
  void applyLevel(uint8_t level) { setLevel(level); } // alias (Ramping example)
  uint8_t getLevel() const { return _level; }
  // dutyMax caps PWM duty (default 127 = 50% — the disc's sweet spot;
  // beyond 50% the resonant push-pull gets weaker, not stronger).
  void setMaxDuty(uint8_t dutyMax) { _dutyMax = dutyMax; }

  // ------------------- current sensing -------------------
  // Sense-amp transfer factor in V per A (gain x shunt).
  // Default 3.0 = INA180A3 (100 V/V) x 30 mOhm. Change if your build differs.
  void setCurrentSenseFactor(float voltsPerAmp) { _senseFactor = voltsPerAmp; }
  // Mean current over `sampleMs` of continuous ADC reads, in mA.
  float readCurrentMa(uint16_t sampleMs = 50);

  // Manual thresholds (mA). See autoCalibrateSense() for what they mean.
  void setSenseThresholds(float discPresentMa, float waterLowMa,
                          float discDisconnectedMa);

  // Auto-calibration. Run this ONCE with the disc attached and sitting in
  // a full water container. It probes the disc at low and working duty,
  // then derives all three thresholds from what it measures.
  // Returns true on success (readings were plausible). Thresholds are kept
  // in RAM only — print them and hard-code via setSenseThresholds(), or
  // save to NVS/Preferences yourself if you want them to survive reboot.
  bool autoCalibrateSense();

  // Probe = briefly drive the disc at a probe duty, measure current,
  // classify, then restore whatever the mist was doing before.
  MistSenseState probe();                 // full classify (disc + water)
  bool discPresent();                     // quick low-duty presence check
  MistSenseState senseState() const { return _senseState; }
  float lastProbeMa() const { return _lastProbeMa; }

  // ------------------- battery (needs a battery pin) -------------------
  // Divider ratio = (Rtop+Rbottom)/Rbottom. Battery Kit V0.3 uses 2.0 (1:1).
  void setBatteryDivider(float ratio) { _battDivider = ratio; }
  // Thresholds in volts (defaults: low 3.45 V, critical 3.20 V under load).
  void setBatteryThresholds(float lowV, float criticalV);
  float readBatteryVolts(uint8_t samples = 16);
  // 0..100% rough LiPo state-of-charge estimate from voltage.
  uint8_t batteryPercent();
  // Classified with hysteresis so it won't flap around the threshold.
  MistBatteryState batteryState();
  bool batteryLow()      { MistBatteryState s = batteryState();
                           return s == MIST_BATT_LOW || s == MIST_BATT_CRITICAL; }
  bool batteryCritical() { return batteryState() == MIST_BATT_CRITICAL; }

  // Graceful power-down: mist off, boost rail off, LED off. Call before
  // esp_deep_sleep_start() to avoid brown-out resets on an empty battery.
  void shutdown();

private:
  float probeAtDuty(uint8_t duty, uint16_t settleMs, uint16_t sampleMs);
  void applyDuty(uint8_t duty);

  int8_t _mistPin, _enPin, _sensePin, _ledPin, _buttonPin, _battPin;
  int _pwmFreq, _pwmRes, _dutyCycle;
  uint8_t _dutyMax;
  uint8_t _level;
  bool _state;
  unsigned long _startTime;
  unsigned long _lastPrintTime;

  // current sense
  float _senseFactor;       // V per A
  float _thDiscPresentMa;   // above this at probe duty -> disc present
  float _thWaterLowMa;      // below this at water duty -> water low
  float _thDiscDisconnMa;   // below this at water duty -> disc fell off
  uint8_t _probeDuty;       // low duty for presence probe (default 10)
  uint8_t _waterProbeDuty;  // working duty for water probe (default 64)
  MistSenseState _senseState;
  float _lastProbeMa;

  // battery
  float _battDivider;
  float _battLowV, _battCritV;
  MistBatteryState _battState;
};

#endif
