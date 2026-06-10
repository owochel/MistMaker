#include "MistMaker.h"

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------
MistMaker::MistMaker(int mistPin, int enPin, int sensePin, int ledPin,
                     int pwmFreq, int pwmRes, int duty)
  : _mistPin(mistPin), _enPin(enPin), _sensePin(sensePin), _ledPin(ledPin),
    _buttonPin(-1), _battPin(-1),
    _pwmFreq(pwmFreq), _pwmRes(pwmRes), _dutyCycle(duty),
    _dutyMax(127), _level(0), _state(false), _lastPrintTime(0),
    _senseFactor(3.0f),
    _thDiscPresentMa(10.0f), _thWaterLowMa(110.0f), _thDiscDisconnMa(70.0f),
    _probeDuty(10), _waterProbeDuty(64),
    _senseState(MIST_SENSE_UNKNOWN), _lastProbeMa(0.0f),
    _battDivider(2.0f), _battLowV(3.45f), _battCritV(3.20f),
    _battState(MIST_BATT_UNKNOWN) {}

MistMaker::MistMaker(const MistMakerPins &p, int pwmFreq, int pwmRes, int duty)
  : MistMaker(p.mist, p.boostEn, p.sense, p.led, pwmFreq, pwmRes, duty) {
  _buttonPin = p.button;
  _battPin   = p.battery;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void MistMaker::begin() {
  if (_ledPin >= 0)    pinMode(_ledPin, OUTPUT);
  if (_enPin >= 0)     pinMode(_enPin, OUTPUT);
  if (_sensePin >= 0)  pinMode(_sensePin, INPUT);
  if (_battPin >= 0)   pinMode(_battPin, INPUT);
  if (_buttonPin >= 0) pinMode(_buttonPin, INPUT); // PCB has its own pull-down

  // NOTE: we deliberately do NOT call analogReadResolution() or
  // analogSetPinAttenuation() — on ESP32-C6 + arduino-esp32 v3.x those calls
  // have been observed to leave the ADC stuck at 0. Core defaults
  // (12-bit, ~3.3 V full scale) are what every threshold here assumes.

  ledcAttach(_mistPin, _pwmFreq, _pwmRes);
  ledcWrite(_mistPin, 0);
  if (_enPin >= 0) digitalWrite(_enPin, LOW);

  _startTime = millis();
}

// ---------------------------------------------------------------------------
// Basic control
// ---------------------------------------------------------------------------
void MistMaker::applyDuty(uint8_t duty) {
  ledcWrite(_mistPin, duty);
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
  digitalWrite(_mistPin, LOW);
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
  // 1..255 -> 1.._dutyMax, rounding so level 255 lands exactly on dutyMax.
  uint8_t duty = (uint8_t)(((uint16_t)level * _dutyMax + 127) / 255);
  if (duty == 0) duty = 1;
  applyDuty(duty);
  _state = true;
}

// ---------------------------------------------------------------------------
// Current sensing
// ---------------------------------------------------------------------------
float MistMaker::readCurrentVoltage() {
  // v1.0 compat: raw sense-pin voltage with the original 2x scale.
  if (_sensePin < 0) return 0.0f;
  return (analogRead(_sensePin) / 4095.0f) * 3.3f * 2.0f;
}

float MistMaker::readCurrentMa(uint16_t sampleMs) {
  if (_sensePin < 0) return 0.0f;
  uint32_t sum = 0, n = 0;
  const uint32_t start = millis();
  while (millis() - start < sampleMs) {
    sum += analogRead(_sensePin);
    n++;
  }
  if (n == 0) return 0.0f;
  const float volts = (float(sum) / float(n)) * 3.3f / 4095.0f;
  return volts * 1000.0f / _senseFactor;
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
float MistMaker::probeAtDuty(uint8_t duty, uint16_t settleMs, uint16_t sampleMs) {
  if (_sensePin < 0) return 0.0f;
  const bool boostWasOff = (_enPin >= 0) && (digitalRead(_enPin) == LOW);
  if (boostWasOff) { digitalWrite(_enPin, HIGH); delay(20); } // rail soft-start

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
  const float ma = probeAtDuty(_probeDuty, 50, 100);
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
  const float ma = probeAtDuty(_waterProbeDuty, 80, 150);
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

  const float lowMa   = probeAtDuty(_probeDuty, 100, 200);      // presence ref
  const float waterMa = probeAtDuty(_waterProbeDuty, 150, 300); // wet ref

  Serial.print(F("[MistMaker] probe duty "));  Serial.print(_probeDuty);
  Serial.print(F(" -> "));  Serial.print(lowMa, 1);  Serial.println(F(" mA"));
  Serial.print(F("[MistMaker] water duty ")); Serial.print(_waterProbeDuty);
  Serial.print(F(" -> "));  Serial.print(waterMa, 1); Serial.println(F(" mA"));

  // Plausibility: a real disc in water draws clearly measurable current at
  // both duties. If not, the disc is missing/dry and calibration would only
  // bake in garbage thresholds.
  if (lowMa < 2.0f || waterMa < 20.0f || waterMa <= lowMa) {
    Serial.println(F("[MistMaker] Calibration FAILED - check disc and water."));
    return false;
  }

  // Thresholds sit between the populated reading and zero/dry territory:
  //   disc present  = 50% of the wet low-duty reading
  //   water low     = 75% of the wet working-duty reading
  //   disconnected  = 50% of the wet working-duty reading
  setSenseThresholds(lowMa * 0.50f, waterMa * 0.75f, waterMa * 0.50f);

  Serial.println(F("[MistMaker] Calibrated thresholds (mA):"));
  Serial.print(F("  discPresent = ")); Serial.println(_thDiscPresentMa, 1);
  Serial.print(F("  waterLow    = ")); Serial.println(_thWaterLowMa, 1);
  Serial.print(F("  discDisconn = ")); Serial.println(_thDiscDisconnMa, 1);
  Serial.println(F("[MistMaker] Hard-code these with setSenseThresholds() to skip calibration next boot."));
  _senseState = MIST_WATER_OK;
  return true;
}

// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------
void MistMaker::setBatteryThresholds(float lowV, float criticalV) {
  _battLowV  = lowV;
  _battCritV = criticalV;
}

float MistMaker::readBatteryVolts(uint8_t samples) {
  if (_battPin < 0) return 0.0f;
  if (samples == 0) samples = 1;
  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; i++) sum += analogRead(_battPin);
  const float pinV = (float(sum) / samples) * 3.3f / 4095.0f;
  return pinV * _battDivider;
}

uint8_t MistMaker::batteryPercent() {
  // Rough open-ish-circuit LiPo curve, clamped 3.3-4.2 V. Good enough for a
  // UI gauge; don't use it for cutoff decisions (use batteryState()).
  const float v = readBatteryVolts();
  if (v <= 0.5f) return 0; // no battery pin / not connected
  float pct = (v - 3.30f) / (4.20f - 3.30f) * 100.0f;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return (uint8_t)(pct + 0.5f);
}

MistBatteryState MistMaker::batteryState() {
  if (_battPin < 0) return MIST_BATT_UNKNOWN;
  const float v = readBatteryVolts();
  // 50 mV hysteresis so we don't flap when the mist load sags the rail.
  switch (_battState) {
    case MIST_BATT_CRITICAL:
      if (v > _battCritV + 0.05f) _battState = MIST_BATT_LOW;
      break;
    case MIST_BATT_LOW:
      if (v <= _battCritV)            _battState = MIST_BATT_CRITICAL;
      else if (v > _battLowV + 0.05f) _battState = MIST_BATT_OK;
      break;
    default: // OK or UNKNOWN
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
    Serial.print(readCurrentMa(20), 1);
    Serial.print(F(" mA"));
  }
  if (_battPin >= 0) {
    Serial.print(F(". Battery: "));
    Serial.print(readBatteryVolts(), 2);
    Serial.print(F(" V ("));
    Serial.print(batteryPercent());
    Serial.print(F("%)"));
  }
  Serial.println();

  _lastPrintTime = millis();
}
