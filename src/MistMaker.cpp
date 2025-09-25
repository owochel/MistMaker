#include "MistMaker.h"

MistMaker::MistMaker(int mistPin, int enPin, int sensePin, int ledPin, int pwmFreq, int pwmRes, int duty)
  : _mistPin(mistPin), _enPin(enPin), _sensePin(sensePin), _ledPin(ledPin),
    _pwmFreq(pwmFreq), _pwmRes(pwmRes), _dutyCycle(duty), _state(false), _lastPrintTime(0) {}

void MistMaker::begin() {
  pinMode(_ledPin, OUTPUT);
  pinMode(_enPin, OUTPUT);
  pinMode(_sensePin, INPUT);

  ledcAttach(_mistPin, _pwmFreq, _pwmRes);
  ledcWrite(_mistPin, 0);
  digitalWrite(_enPin, LOW);

  _startTime = millis();
}

void MistMaker::turnOn() {
  digitalWrite(_enPin, HIGH);
  digitalWrite(_ledPin, HIGH);
  ledcWrite(_mistPin, _dutyCycle);
  _state = true;
  Serial.println("Mist maker ON - 50% power");
}

void MistMaker::turnOff() {
  digitalWrite(_enPin, LOW);
  digitalWrite(_ledPin, LOW);
  ledcWrite(_mistPin, 0);
  digitalWrite(_mistPin, LOW);
  _state = false;
  Serial.println("Mist maker OFF");
}

void MistMaker::toggle() {
  _state ? turnOff() : turnOn();
}

bool MistMaker::isOn() {
  return _state;
}

float MistMaker::readCurrentVoltage() {
  return (analogRead(_sensePin) / 4095.0) * 3.3 * 2.0;
}

void MistMaker::printStatus() {
  float currentVoltage = readCurrentVoltage();
  unsigned long runtime = (millis() - _startTime) / 1000;

  Serial.print("Runtime: ");
  Serial.print(runtime);
  Serial.print(" seconds. Mist maker is ");
  Serial.print(_state ? "ON" : "OFF");
  Serial.print(". Current sense: ");
  Serial.print(currentVoltage, 2);
  Serial.println(" V");

  _lastPrintTime = millis();
}

void MistMaker::setDuty(uint8_t duty) {
  _dutyCycle = duty;
  if (_state) {
    ledcWrite(_mistPin, _dutyCycle);
  }
}

void MistMaker::applyLevel(uint8_t level) {
  if (level == 0) {
    turnOff();
    return;
  }
  if (!_state) {
    digitalWrite(_enPin, HIGH);
    digitalWrite(_ledPin, HIGH);
    _state = true;
  }
  setDuty(level);
}