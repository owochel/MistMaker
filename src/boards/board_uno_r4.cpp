// Arduino Uno R4 Minima / WiFi (Renesas RA4M1) — PWM via the core's PwmOut
// class on a 48 MHz GPT timer. On both variants pin 9 sits alone on GPT
// channel 7, so analogWrite / Servo / tone on other pins don't disturb it.
#if defined(ARDUINO_ARCH_RENESAS_UNO)

#include "MistMakerBoard.h"
#include "pwm.h"

namespace {
  PwmOut* pwm = nullptr;   // the one mist channel (created once in pwmInit)
  int8_t pwmPin = -1;
}

namespace MistMakerBoard {

uint16_t pwmInit(int8_t pin, uint32_t freqHz, uint8_t resBits) {
  (void)resBits;  // resolution comes from the timer period, not a bit count
  if (pin < 0) return 0;
  if (pwm && pwmPin != pin) return 0;  // one mist channel per sketch
  const uint32_t clockHz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKD);
  const uint32_t period = (clockHz + freqHz / 2) / freqHz;  // 442 counts at 48 MHz
  if (period < 2 || period > 0xFFFF) return 0;
  if (pwm) { pwm->end(); delete pwm; pwm = nullptr; }  // clean re-begin
  pwm = new PwmOut(pin);
  // raw = true: period/pulse are direct timer counts at PCLKD / 1.
  if (!pwm->begin(period, 0, true, TIMER_SOURCE_DIV_1)) {
    delete pwm;                       // begin() fails on non-PWM pins
    pwm = nullptr;
    return 0;
  }
  pwmPin = pin;
  return (uint16_t)period;  // pulse == period is 100% duty
}

void pwmWrite(int8_t pin, uint16_t hwDuty) {
  if (pwm && pin == pwmPin) pwm->pulseWidth_raw(hwDuty);
}

void adcInit() {}

uint16_t adcReadMv(int8_t pin) {
  // 14-bit is a free software re-map on this core; re-assert it per read in
  // case the sketch changed it. The ADC reference is VCC, assumed 5.00 V.
  analogReadResolution(14);
  return (uint16_t)(((uint32_t)analogRead(pin) * 5000UL + 8191UL) / 16383UL);
}

uint16_t senseRead(int8_t pin) {
  analogReadResolution(14);
  return (uint16_t)analogRead(pin);
}

float senseMvPerCount() {
  return 5000.0f / 16383.0f;
}

bool analogCapable(int8_t pin) {
  return pin >= A0 && pin < A0 + NUM_ANALOG_INPUTS;
}

}  // namespace MistMakerBoard

#endif
