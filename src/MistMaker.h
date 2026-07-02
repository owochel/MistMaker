#ifndef MIST_MAKER_H
#define MIST_MAKER_H

#include <Arduino.h>

// ===========================================================================
// MistMaker — Arduino library for the Programmable Mist Maker (OSHWA US002742)
//
// One facade class + a pin preset per official board. Typical use:
//
//   MistMaker mist(MistMakerBatteryKitV04());
//   void setup() { mist.begin(); mist.setLevel(180); }
//
// v2.0 — cleanup release:
//   * every tuning value is a named constant in MistMakerDefaults (below)
//   * constructor `duty` parameter now actually sets the duty cap (it was
//     silently ignored in 1.x)
//   * removed: applyLevel() alias -> use setLevel();
//              readCurrentVoltage() -> use readCurrentMa()
// v1.2   Power-source sensing via the TPS2116 mux ST pin (Battery Kit V0.4):
//        battery monitoring self-gates on USB-vs-cell, can't false-brown-out.
// v1.1   Board presets, dimming, current sensing in mA, disc/water detection,
//        battery monitoring (on shuang cai's v1.0 control API).
//
// Hardware background: the boards sense piezo current through a shunt +
// INA180A3 current-sense amplifier into an ADC pin. A dry disc, a missing
// disc, and a disc in water each draw distinctly different current at a
// given PWM duty — so one ADC pin gives you disc detection AND a water-level
// sensor for free. Default thresholds come from bench measurements on the
// Block Kit V0.1 (XIAO ESP32-C6, INA180A3, 30 mOhm shunt, 2026-05).
// ===========================================================================

// ---------------------------------------------------------------------------
// Defaults — every tunable in one place. Constructor arguments and set*()
// methods override these at runtime; edit here only to rebase the library
// on different hardware.
// ---------------------------------------------------------------------------
namespace MistMakerDefaults {
  // Piezo drive. 108.7 kHz = disc resonance; 8-bit duty; cap at 127 (50%)
  // because beyond 50% the resonant push-pull gets weaker, not stronger.
  constexpr uint32_t PWM_FREQ_HZ  = 108700;
  constexpr uint8_t  PWM_RES_BITS = 8;
  constexpr uint8_t  DUTY_MAX     = 127;

  // Current sense: V per A = amp gain x shunt = INA180A3 (100 V/V) x 30 mOhm.
  constexpr float SENSE_VOLTS_PER_AMP = 3.0f;

  // Classifier thresholds (mA) — see autoCalibrateSense() for their meaning.
  constexpr float TH_DISC_PRESENT_MA = 10.0f;   // >= this at probe duty -> disc there
  constexpr float TH_WATER_LOW_MA    = 110.0f;  // <  this at water duty -> refill
  constexpr float TH_DISC_DISCONN_MA = 70.0f;   // <  this at water duty -> disc fell off
  constexpr uint8_t PROBE_DUTY       = 10;      // gentle presence-check duty
  constexpr uint8_t WATER_PROBE_DUTY = 64;      // working duty for the water probe

  // Battery. Divider ratio = (Rtop+Rbottom)/Rbottom — Battery Kits use
  // 10k/10k = 2.0. Thresholds are volts UNDER LOAD; 50 mV hysteresis keeps
  // batteryState() from flapping when the mist load sags the rail.
  constexpr float BATT_DIVIDER      = 2.0f;
  constexpr float BATT_LOW_V        = 3.45f;
  constexpr float BATT_CRITICAL_V   = 3.20f;
  constexpr float BATT_HYST_V       = 0.05f;
  constexpr uint8_t BATT_SAMPLES    = 16;
  // batteryPercent() linearizes this span (UI gauge only, not for cutoffs).
  constexpr float BATT_EMPTY_V = 3.30f;
  constexpr float BATT_FULL_V  = 4.20f;
}

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
  int8_t usbSense; // power-mux status (TPS2116 ST): HIGH=USB, LOW=cell (-1 = none)
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
  return MistMakerPins{ D0, -1, D2, -1, -1, -1, -1 };
}

// Mist Maker Battery Kit V0.3 — LiPo + USB-C, battery divider on D1.
// No mux-status pin: on V0.3 the TPS2116 ST line isn't routed to the XIAO,
// so battery reads can't tell USB from the cell — call disableBattery().
inline MistMakerPins MistMakerBatteryKitV03() {
  return MistMakerPins{ D0, D3, D2, D7, D6, D1, -1 };
}

// Mist Maker Battery Kit V0.4 — V0.3 plus the TPS2116 ST (power-mux status)
// on D8, so battery monitoring gates itself on the real power source.
inline MistMakerPins MistMakerBatteryKitV04() {
  return MistMakerPins{ D0, D3, D2, D7, D6, D1, D8 };
}

// Block Kit V0.1 — OHS 2026 demo board (reed on D10, IS31FL3731 LEDs on I2C).
inline MistMakerPins MistMakerBlockKitV01() {
  return MistMakerPins{ D0, D3, D2, D7, D6, -1, -1 };
}

// Legacy V1.x single PCB (OHS 2025 workshop board).
inline MistMakerPins MistMakerLegacyV1() {
  return MistMakerPins{ D1, D3, D2, D7, D6, -1, -1 };
}
#endif

// ---------------------------------------------------------------------------
// Classifier results
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
  MIST_BATT_CHARGING,       // on USB (mux on VIN1) — cell is charging, not a SoC
};

class MistMaker {
public:
  // Pin-preset constructor (preferred). `dutyMax` caps the PWM duty that
  // setLevel(255)/turnOn() reach — see MistMakerDefaults::DUTY_MAX.
  MistMaker(const MistMakerPins &pins,
            uint32_t pwmFreq = MistMakerDefaults::PWM_FREQ_HZ,
            uint8_t  pwmRes  = MistMakerDefaults::PWM_RES_BITS,
            uint8_t  dutyMax = MistMakerDefaults::DUTY_MAX);

  // Bare-pins constructor for breadboards / custom wiring (v1.0 signature).
  MistMaker(int mistPin, int enPin, int sensePin, int ledPin,
            uint32_t pwmFreq = MistMakerDefaults::PWM_FREQ_HZ,
            uint8_t  pwmRes  = MistMakerDefaults::PWM_RES_BITS,
            uint8_t  dutyMax = MistMakerDefaults::DUTY_MAX);

  void begin();

  // ------------------- basic control -------------------
  void turnOn();              // full power (= dutyMax)
  void turnOff();
  void toggle();
  bool isOn();
  void printStatus();         // one status line to Serial

  // ------------------- dimming -------------------
  // level 0..255 maps linearly onto 0..dutyMax. 0 = off.
  void setLevel(uint8_t level);
  uint8_t getLevel() const { return _level; }
  void setMaxDuty(uint8_t dutyMax) { _dutyMax = dutyMax; }

  // ------------------- current sensing -------------------
  // Sense-amp transfer factor in V per A (gain x shunt). Change if your
  // build differs from MistMakerDefaults::SENSE_VOLTS_PER_AMP.
  void setCurrentSenseFactor(float voltsPerAmp) { _senseFactor = voltsPerAmp; }
  // Mean current over `sampleMs` of continuous ADC reads, in mA. Blocking.
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
  // classify, then restore whatever the mist was doing before. Blocking
  // for a few hundred ms — schedule probes in an OFF window, not per-loop.
  MistSenseState probe();                 // full classify (disc + water)
  bool discPresent();                     // quick low-duty presence check
  MistSenseState senseState() const { return _senseState; }
  float lastProbeMa() const { return _lastProbeMa; }

  // ------------------- power source (mux status, V0.4+) -------------------
  // The TPS2116 power mux picks USB (VIN1) over the cell (VIN2) whenever USB is
  // present, so the battery is the *source* only when USB is unplugged. Its ST
  // pin (HIGH = USB, LOW = cell) says which — wire it to a GPIO (Battery Kit
  // V0.4 = D8) and battery monitoring gates itself on it, never mistaking
  // "charging on USB" for "dying on the cell". Presets set this; set it by hand
  // for a custom board.
  void setUsbSensePin(int8_t pin) { _usbSensePin = pin; }
  // true when the load runs from USB (mux on VIN1). With no ST/USB-sense pin
  // configured this is ALWAYS true (fail safe: a board that can't sense its
  // source must never blind-auto-shut-down). Consequence: onBattery() is always
  // false on pre-V0.4 boards, so gate low-battery logic on batteryState()
  // (which handles the no-sense case), NOT on onBattery().
  bool usbPresent();
  // true when the load runs from the cell (mux on VIN2) — the only time
  // readBatteryVolts() is a real state-of-charge. Always false without an ST pin.
  bool onBattery() { return !usbPresent(); }

  // ------------------- battery (needs a battery pin) -------------------
  // Turn battery sensing OFF at runtime — the pre-V0.4 escape hatch. Where the
  // mux status isn't wired to a GPIO (Battery Kit V0.3), the D1 divider can't
  // tell USB-C power from a real cell, so the reading is unreliable and caused
  // false low-battery shutdowns. After this call every battery_* method behaves
  // as on a board with no cell: readBatteryVolts()/percent() return 0 and
  // batteryState() returns MIST_BATT_UNKNOWN. On V0.4 prefer wiring ST instead
  // (setUsbSensePin / the V0.4 preset) — then batteryState() self-gates and you
  // keep real monitoring while on the cell.
  void disableBattery() { _battPin = -1; }
  // Divider ratio = (Rtop+Rbottom)/Rbottom. Battery Kits use 2.0 (10k/10k).
  void setBatteryDivider(float ratio) { _battDivider = ratio; }
  // Thresholds in volts under load (defaults in MistMakerDefaults).
  void setBatteryThresholds(float lowV, float criticalV);
  float readBatteryVolts(uint8_t samples = MistMakerDefaults::BATT_SAMPLES);
  // 0..100% rough LiPo state-of-charge estimate from voltage.
  uint8_t batteryPercent();
  // Classified with hysteresis so it won't flap around the threshold. With a
  // USB-sense pin wired and USB present, returns MIST_BATT_CHARGING and never
  // LOW/CRITICAL (the cell is a charge target, not the source).
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

  int8_t _mistPin, _enPin, _sensePin, _ledPin, _buttonPin, _battPin, _usbSensePin;
  uint32_t _pwmFreq;
  uint8_t _pwmRes;
  uint8_t _dutyMax;
  uint8_t _level;
  bool _state;
  unsigned long _startTime;

  // current sense
  float _senseFactor;       // V per A
  float _thDiscPresentMa;   // above this at probe duty -> disc present
  float _thWaterLowMa;      // below this at water duty -> water low
  float _thDiscDisconnMa;   // below this at water duty -> disc fell off
  uint8_t _probeDuty;       // low duty for presence probe
  uint8_t _waterProbeDuty;  // working duty for water probe
  MistSenseState _senseState;
  float _lastProbeMa;

  // battery
  float _battDivider;
  float _battLowV, _battCritV;
  MistBatteryState _battState;
};

#endif
