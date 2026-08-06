#include "MistMaker.h"
#include "boards/MistMakerBoard.h"

namespace {
  // Probe choreography (ms). Settle = let the drive + water column respond
  // before measuring; sample = averaging window. Calibration settles longer
  // because its numbers get baked into thresholds.
  constexpr uint16_t PRESENCE_SETTLE_MS = 50,  PRESENCE_SAMPLE_MS = 100;
  constexpr uint16_t WATER_SETTLE_MS    = 80,  WATER_SAMPLE_MS    = 150;
  constexpr uint16_t CAL_LOW_SETTLE_MS  = 100, CAL_LOW_SAMPLE_MS  = 200;
  constexpr uint16_t CAL_WET_SETTLE_MS  = 150, CAL_WET_SAMPLE_MS  = 300;
  constexpr uint16_t BOOST_SOFTSTART_MS = 20;

  // autoCalibrateSense(): thresholds as fractions of the wet references,
  // and minimum plausible wet readings (mA) below which we refuse to
  // calibrate (disc missing/dry would bake in garbage).
  constexpr float CAL_PRESENT_FRACTION = 0.50f;  // of wet low-duty reading
  constexpr float CAL_WATERLOW_FRACTION = 0.75f; // of wet working-duty reading
  constexpr float CAL_DISCONN_FRACTION  = 0.50f; // of wet working-duty reading
  constexpr float CAL_MIN_LOW_MA = 2.0f, CAL_MIN_WET_MA = 20.0f;

  // printStatus() uses a shorter averaging window than a real measurement —
  // it's a glance, not a reading.
  constexpr uint16_t STATUS_SAMPLE_MS = 20;

  // Below any plausible cell voltage -> battery pin floating / not connected.
  constexpr float BATT_ABSENT_V = 0.5f;

  // usbPresent() analog threshold: ST reads ~3.1 V on USB, ~0 V on the cell.
  constexpr uint16_t ST_USB_THRESHOLD_MV = 2000;
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------
MistMaker::MistMaker(int mistPin, int enPin, int sensePin, int ledPin,
                     int buttonPin, int battPin, int usbSensePin,
                     uint32_t pwmFreq, uint8_t pwmRes, int dutyMax)
  : _mistPin(mistPin), _enPin(enPin), _sensePin(sensePin), _ledPin(ledPin),
    _buttonPin(buttonPin), _battPin(battPin), _usbSensePin(usbSensePin),
    _pwmFreq(pwmFreq), _pwmRes(pwmRes < 1 ? 1 : (pwmRes > 16 ? 16 : pwmRes)),
    // _pwmRes is initialized above (declaration order), so this is safe:
    _dutyMax(resolveDutyCap(dutyMax)), _hwTop(0),
    _level(0), _state(false), _startTime(0),
    _senseFactor(MistMakerDefaults::SENSE_VOLTS_PER_AMP),
    _thDiscPresentMa(MistMakerDefaults::TH_DISC_PRESENT_MA),
    _thWaterLowMa(MistMakerDefaults::TH_WATER_LOW_MA),
    _thDiscDisconnMa(MistMakerDefaults::TH_DISC_DISCONN_MA),
    _probeDuty(MistMakerDefaults::PROBE_DUTY),
    _waterProbeDuty(MistMakerDefaults::WATER_PROBE_DUTY),
    _senseState(MIST_SENSE_UNKNOWN), _lastProbeMa(0.0f),
    _battDivider(MistMakerDefaults::BATT_DIVIDER),
    _battLowV(MistMakerDefaults::BATT_LOW_V),
    _battCritV(MistMakerDefaults::BATT_CRITICAL_V),
    _battState(MIST_BATT_UNKNOWN) {}

MistMaker::MistMaker(const MistMakerPins &p,
                     uint32_t pwmFreq, uint8_t pwmRes, int dutyMax)
  : MistMaker(p.mist, p.boostEn, p.sense, p.led,
              p.button, p.battery, p.usbSense,
              pwmFreq, pwmRes, dutyMax) {}

uint16_t MistMaker::resolveDutyCap(int requested) const {
  // 1UL: at pwmRes 16 a 16-bit shift would overflow on AVR.
  const uint16_t fullScale = (uint16_t)((1UL << _pwmRes) - 1UL);
  // ~90% of full scale is the physical ceiling: above it the resonant
  // ring-back has no time to swing, so the drive makes heat, not mist
  // (bench sweep, 2026-07-03). Requests beyond it clamp to the ceiling.
  const uint16_t ceiling = (uint16_t)(((uint32_t)fullScale * 9u) / 10u);
  if (requested >= 1 && requested <= (int)ceiling) return (uint16_t)requested;
  if (requested > (int)ceiling) return ceiling;
  // DUTY_AUTO, 0, or negatives: 1/3 of full scale (= 85 at 8-bit) — the
  // thermal sweet spot. Bench-tested as producing a good amount of mist while
  // keeping the tapped inductor cool. Expressed as a fraction, not a literal
  // 85, so it stays 33% at any configured PWM resolution.
  const uint16_t third = (uint16_t)(fullScale / 3);
  return third < 1 ? 1 : third;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
bool MistMaker::begin() {
  if (_ledPin >= 0)    pinMode(_ledPin, OUTPUT);
  if (_enPin >= 0)     pinMode(_enPin, OUTPUT);
  if (_sensePin >= 0)  pinMode(_sensePin, INPUT);
  if (_battPin >= 0)   pinMode(_battPin, INPUT);
  if (_buttonPin >= 0) pinMode(_buttonPin, INPUT); // PCB has its own pull-down
  // Mux ST (TPS2116 pin 8) is level-shifted by an external divider on V0.4, so
  // read it as a plain high-Z INPUT — an internal pull would swamp the divider.
  if (_usbSensePin >= 0) pinMode(_usbSensePin, INPUT);

  MistMakerBoard::adcInit();
  _hwTop = MistMakerBoard::pwmInit(_mistPin, _pwmFreq, _pwmRes);
  if (_enPin >= 0) digitalWrite(_enPin, LOW);

  _startTime = millis();
  if (_hwTop == 0) {
    Serial.print(F("[MistMaker] Mist pin can't make the mist signal here. "));
    Serial.println(F(MISTMAKER_MIST_PIN_HINT));
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Basic control
// ---------------------------------------------------------------------------
void MistMaker::applyDuty(uint16_t duty) {
  if (_hwTop == 0) return;  // begin() not run, or the mist pin was rejected
  // Logical 0..fullScale -> hardware 0..top, rounding half-up. On boards whose
  // timer top equals the logical full scale this is an exact identity.
  const uint16_t fullScale = (uint16_t)((1UL << _pwmRes) - 1UL);
  uint32_t hw = ((uint32_t)duty * _hwTop + fullScale / 2u) / fullScale;
  if (duty > 0 && hw == 0) hw = 1;
  MistMakerBoard::pwmWrite(_mistPin, (uint16_t)hw);
}

void MistMaker::turnOn() {
  if (_enPin >= 0) digitalWrite(_enPin, HIGH);
  if (_ledPin >= 0) digitalWrite(_ledPin, HIGH);
  _level = 255;
  applyDuty(_dutyMax);
  _state = true;
}

void MistMaker::turnOff() {
  if (_enPin >= 0) digitalWrite(_enPin, LOW);
  if (_ledPin >= 0) digitalWrite(_ledPin, LOW);
  applyDuty(0);
  digitalWrite(_mistPin, LOW);  // safety belt if the pin ever leaves the timer
  _level = 0;
  _state = false;
}

void MistMaker::toggle() {
  _state ? turnOff() : turnOn();
}

bool MistMaker::isOn() {
  return _state;
}

void MistMaker::setLevel(uint8_t level) {
  _level = level;
  if (level == 0) { turnOff(); return; }
  if (_enPin >= 0) digitalWrite(_enPin, HIGH);
  if (_ledPin >= 0) digitalWrite(_ledPin, HIGH);
  // 1..255 -> 1.._dutyMax; + 255/2 rounds half-up so 255 lands on dutyMax.
  uint16_t duty = (uint16_t)(((uint32_t)level * _dutyMax + (255 / 2)) / 255);
  if (duty == 0) duty = 1;
  applyDuty(duty);
  _state = true;
}

// ---------------------------------------------------------------------------
// Current sensing
// ---------------------------------------------------------------------------
float MistMaker::readCurrentMa(uint16_t sampleMs) {
  if (_sensePin < 0) return 0.0f;
  uint32_t sum = 0, n = 0;
  const uint32_t start = millis();
  while (millis() - start < sampleMs) {
    sum += MistMakerBoard::senseRead(_sensePin);
    n++;
  }
  if (n == 0) return 0.0f;
  const float mv = (float(sum) / float(n)) * MistMakerBoard::senseMvPerCount();
  return mv / _senseFactor;  // mV / (V per A) = mA
}

void MistMaker::setSenseThresholds(float discPresentMa, float waterLowMa,
                                   float discDisconnectedMa) {
  _thDiscPresentMa = discPresentMa;
  _thWaterLowMa    = waterLowMa;
  _thDiscDisconnMa = discDisconnectedMa;
}

// Drive at `duty` for settleMs, average current for sampleMs, then restore
// whatever the mist was doing before. The boost rail is held on during the
// probe so back-to-back probes don't churn the converter.
float MistMaker::probeAtDuty(uint16_t duty, uint16_t settleMs, uint16_t sampleMs) {
  if (_sensePin < 0) return 0.0f;
  const bool boostWasOff = (_enPin >= 0) && (digitalRead(_enPin) == LOW);
  if (boostWasOff) { digitalWrite(_enPin, HIGH); delay(BOOST_SOFTSTART_MS); }

  applyDuty(duty);
  delay(settleMs);
  const float ma = readCurrentMa(sampleMs);

  // restore previous drive state
  if (_state && _level > 0) setLevel(_level);
  else {
    applyDuty(0);
    if (boostWasOff) digitalWrite(_enPin, LOW);
  }
  return ma;
}

bool MistMaker::discPresent() {
  const float ma = probeAtDuty(_probeDuty, PRESENCE_SETTLE_MS, PRESENCE_SAMPLE_MS);
  _lastProbeMa = ma;
  const bool present = ma >= _thDiscPresentMa;
  if (!present) _senseState = MIST_DISC_MISSING;
  return present;
}

MistSenseState MistMaker::probe() {
  if (_sensePin < 0) { _senseState = MIST_SENSE_UNKNOWN; return _senseState; }

  // Step 1: cheap low-duty presence check.
  if (!discPresent()) return _senseState; // MIST_DISC_MISSING

  // Step 2: water probe at working duty.
  const float ma = probeAtDuty(_waterProbeDuty, WATER_SETTLE_MS, WATER_SAMPLE_MS);
  _lastProbeMa = ma;
  if (ma < _thDiscDisconnMa)      _senseState = MIST_DISC_DISCONNECTED;
  else if (ma < _thWaterLowMa)    _senseState = MIST_WATER_LOW;
  else                            _senseState = MIST_WATER_OK;
  return _senseState;
}

bool MistMaker::autoCalibrateSense() {
  if (_sensePin < 0) return false;

  // The disc must be attached and in water — tell the user what we expect.
  Serial.println(F("[MistMaker] Auto-calibrating current sense."));
  Serial.println(F("[MistMaker] Disc must be attached and sitting in water!"));

  const float lowMa   = probeAtDuty(_probeDuty, CAL_LOW_SETTLE_MS, CAL_LOW_SAMPLE_MS);
  const float waterMa = probeAtDuty(_waterProbeDuty, CAL_WET_SETTLE_MS, CAL_WET_SAMPLE_MS);

  Serial.print(F("[MistMaker] probe duty "));  Serial.print(_probeDuty);
  Serial.print(F(" -> "));  Serial.print(lowMa, 1);  Serial.println(F(" mA"));
  Serial.print(F("[MistMaker] water duty ")); Serial.print(_waterProbeDuty);
  Serial.print(F(" -> "));  Serial.print(waterMa, 1); Serial.println(F(" mA"));

  // Plausibility: a real disc in water draws clearly measurable current at
  // both duties. If not, the disc is missing/dry and calibration would only
  // bake in garbage thresholds.
  if (lowMa < CAL_MIN_LOW_MA || waterMa < CAL_MIN_WET_MA || waterMa <= lowMa) {
    Serial.println(F("[MistMaker] Calibration FAILED - check disc and water."));
    return false;
  }

  setSenseThresholds(lowMa   * CAL_PRESENT_FRACTION,
                     waterMa * CAL_WATERLOW_FRACTION,
                     waterMa * CAL_DISCONN_FRACTION);

  Serial.println(F("[MistMaker] Calibrated thresholds (mA):"));
  Serial.print(F("  discPresent = ")); Serial.println(_thDiscPresentMa, 1);
  Serial.print(F("  waterLow    = ")); Serial.println(_thWaterLowMa, 1);
  Serial.print(F("  discDisconn = ")); Serial.println(_thDiscDisconnMa, 1);
  Serial.println(F("[MistMaker] Hard-code these with setSenseThresholds() to skip calibration next boot."));
  _senseState = MIST_WATER_OK;
  return true;
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------
void MistMaker::setButtonPin(int8_t pin) {
  _buttonPin = pin;
  // Safe to call before or after begin(): re-apply pinMode when enabling.
  if (_buttonPin >= 0) pinMode(_buttonPin, INPUT); // PCB has its own pull-down
}

bool MistMaker::buttonPressed() const {
  if (_buttonPin < 0) return false;
  // Active-HIGH: external pull-down on the PCB holds LOW until pressed.
  return digitalRead(_buttonPin) == HIGH;
}

// ---------------------------------------------------------------------------
// Battery + power source
// ---------------------------------------------------------------------------
void MistMaker::setUsbSensePin(int8_t pin) {
  _usbSensePin = pin;
  // Mux ST is level-shifted by an external divider on V0.4 — plain INPUT,
  // no internal pull (same as begin()).
  if (_usbSensePin >= 0) pinMode(_usbSensePin, INPUT);
}

void MistMaker::setBatteryPin(int8_t pin) {
  _battPin = pin;
  // A newly selected (or re-enabled) ADC source must be classified fresh;
  // otherwise LOW/CRITICAL hysteresis from the previous source can leak into
  // the first reading from this pin.
  _battState = MIST_BATT_UNKNOWN;
  if (_battPin >= 0) pinMode(_battPin, INPUT);
}

void MistMaker::setBatteryThresholds(float lowV, float criticalV) {
  _battLowV  = lowV;
  _battCritV = criticalV;
}

float MistMaker::readBatteryVolts(uint8_t samples) {
  if (_battPin < 0) return 0.0f;
  if (samples == 0) samples = 1;
  uint32_t sum_mV = 0;
  for (uint8_t i = 0; i < samples; i++) sum_mV += MistMakerBoard::adcReadMv(_battPin);
  const float pinV = (float(sum_mV) / samples) / 1000.0f;
  return pinV * _battDivider;
}

uint8_t MistMaker::batteryPercent() {
  // Rough open-ish-circuit LiPo curve, clamped EMPTY..FULL. Good enough for
  // a UI gauge; don't use it for cutoff decisions (use batteryState()).
  const float v = readBatteryVolts();
  if (v <= BATT_ABSENT_V) return 0; // no battery pin / not connected
  const float pct = (v - MistMakerDefaults::BATT_EMPTY_V) /
              (MistMakerDefaults::BATT_FULL_V - MistMakerDefaults::BATT_EMPTY_V) * 100.0f;
  return (uint8_t)(constrain(pct, 0.0f, 100.0f) + 0.5f); // round half-up
}

bool MistMaker::usbPresent() {
  // ST HIGH = mux sourcing VIN1 (USB), LOW = VIN2 (cell). See TPS2116 §7.3.3.
  // No sense pin -> assume USB present (fail safe: don't blind-shutdown).
  if (_usbSensePin < 0) return true;
  // On 5 V boards ST's ~3.1 V high is marginal for digitalRead, so boards that
  // can read it as analog do (threshold well between LOW ~0 V and HIGH ~3.1 V).
  if (MistMakerBoard::analogCapable(_usbSensePin))
    return MistMakerBoard::adcReadMv(_usbSensePin) >= ST_USB_THRESHOLD_MV;
  return digitalRead(_usbSensePin) == HIGH;
}

MistBatteryState MistMaker::batteryState() {
  if (_battPin < 0) return MIST_BATT_UNKNOWN;

  // Gate on the power mux: with a USB-sense pin wired (V0.4), a HIGH ST means
  // the load runs from USB and the cell is a charge target — its voltage
  // tracks the charger, not state-of-charge. Report CHARGING and never
  // LOW/CRITICAL. This is the fix for the V0.3 false brown-out on USB.
  if (_usbSensePin >= 0 && usbPresent()) {
    // Report CHARGING but do NOT overwrite _battState: a transient ST HIGH (mux
    // chatter, or a floating pin on a V0.3 board) must not wipe the LOW/CRITICAL
    // hysteresis latch, or a cell sitting right at the threshold gets released
    // early when we drop back to it. The latch resumes untouched on the cell.
    return MIST_BATT_CHARGING;
  }

  const float v = readBatteryVolts();
  // Hysteresis so we don't flap when the mist load sags the rail.
  const float hyst = MistMakerDefaults::BATT_HYST_V;
  switch (_battState) {
    case MIST_BATT_CRITICAL:
      if (v > _battCritV + hyst) _battState = MIST_BATT_LOW;
      break;
    case MIST_BATT_LOW:
      if (v <= _battCritV)          _battState = MIST_BATT_CRITICAL;
      else if (v > _battLowV + hyst) _battState = MIST_BATT_OK;
      break;
    default: // OK or UNKNOWN — classify fresh (CHARGING never latches here)
      if (v <= _battCritV)      _battState = MIST_BATT_CRITICAL;
      else if (v <= _battLowV)  _battState = MIST_BATT_LOW;
      else                      _battState = MIST_BATT_OK;
      break;
  }
  return _battState;
}

void MistMaker::shutdown() {
  turnOff();                                  // mist + LED off, boost EN low
  Serial.println(F("[MistMaker] shutdown(): mist off, boost rail off."));
  Serial.flush();
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
void MistMaker::printStatus() {
  const unsigned long runtime = (millis() - _startTime) / 1000;

  Serial.print(F("Runtime: "));
  Serial.print(runtime);
  Serial.print(F(" s. Mist "));
  Serial.print(_state ? F("ON") : F("OFF"));
  Serial.print(F(" level "));
  Serial.print(_level);

  if (_sensePin >= 0) {
    Serial.print(F(". Current: "));
    Serial.print(readCurrentMa(STATUS_SAMPLE_MS), 1);
    Serial.print(F(" mA"));
  }
  if (_battPin >= 0) {
    Serial.print(F(". Battery: "));
    Serial.print(readBatteryVolts(), 2);
    Serial.print(F(" V"));
    if (_usbSensePin >= 0 && usbPresent()) {
      Serial.print(F(" (USB charging)"));
    } else {
      Serial.print(F(" ("));
      Serial.print(batteryPercent());
      Serial.print(F("%)"));
    }
  }
  Serial.println();
}
