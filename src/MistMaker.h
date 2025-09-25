#ifndef MIST_MAKER_H
#define MIST_MAKER_H

#include <Arduino.h>

class MistMaker {
public:
  MistMaker(int mistPin, int enPin, int sensePin, int ledPin, int pwmFreq = 108700, int pwmRes = 8, int duty = 127);

  void begin();
  void turnOn();
  void turnOff();
  void toggle();
  void printStatus();
  bool isOn();
  float readCurrentVoltage();
  void setDuty(uint8_t duty);
  void applyLevel(uint8_t level);

private:
  int _mistPin, _enPin, _sensePin, _ledPin;
  int _pwmFreq, _pwmRes, _dutyCycle;
  bool _state;
  unsigned long _startTime;
  unsigned long _lastPrintTime;
};

#endif