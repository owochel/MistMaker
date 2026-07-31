// Arduino Nano 33 BLE / BLE Sense (nRF52840, mbed core) — PWM via the chip's
// PWM3 peripheral at 16 MHz. The nRF routes PWM to any GPIO, so every digital
// pin can make mist (the guides use 9). analogWrite/tone/Servo use PWM0-2 and
// keep working alongside — just don't run more than three of them at once.
#if defined(ARDUINO_ARCH_NRF52840)

#include "MistMakerBoard.h"
#include "mbed.h"
#include "nrf.h"
#include "pinDefinitions.h"

namespace {
  NRF_PWM_Type* const kPwm = NRF_PWM3;
  volatile uint16_t seqValue;  // DMA-read by the PWM; bit 15 = normal polarity
  bool sleepLocked = false;
}

namespace MistMakerBoard {

uint16_t pwmInit(int8_t pin, uint32_t freqHz, uint8_t resBits) {
  (void)resBits;  // resolution comes from the counter top, not a bit count
  if (pin < 0) return 0;
  const PinName name = digitalPinToPinName(pin);
  if (name == NC) return 0;
  const uint32_t top = (16000000UL + freqHz / 2) / freqHz;  // 147 -> 108.84 kHz
  if (top < 2 || top > 0x7FFF) return 0;

  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  kPwm->ENABLE = 0;
  kPwm->PSEL.OUT[0] = (uint32_t)name;  // PinName encodes port/pin as PSEL wants
  kPwm->PSEL.OUT[1] = 0xFFFFFFFF;      // disconnected
  kPwm->PSEL.OUT[2] = 0xFFFFFFFF;
  kPwm->PSEL.OUT[3] = 0xFFFFFFFF;
  kPwm->MODE = PWM_MODE_UPDOWN_Up;
  kPwm->PRESCALER = PWM_PRESCALER_PRESCALER_DIV_1;  // 16 MHz ticks
  kPwm->COUNTERTOP = top;
  kPwm->LOOP = 0;
  kPwm->DECODER = (PWM_DECODER_LOAD_Common << PWM_DECODER_LOAD_Pos) |
                  (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);
  seqValue = 0x8000;  // duty 0, normal polarity -> solid LOW
  kPwm->SEQ[0].PTR = (uint32_t)&seqValue;
  kPwm->SEQ[0].CNT = 1;
  kPwm->SEQ[0].REFRESH = 0;
  kPwm->SEQ[0].ENDDELAY = 0;
  kPwm->ENABLE = 1;
  kPwm->TASKS_SEQSTART[0] = 1;
  // mbed's deep sleep (entered during idle delay()) stops the 16 MHz PWM
  // clock and with it the mist — keep the system in shallow sleep instead.
  if (!sleepLocked) {
    sleep_manager_lock_deep_sleep();
    sleepLocked = true;
  }
  return (uint16_t)top;
}

void pwmWrite(int8_t pin, uint16_t hwDuty) {
  (void)pin;
  seqValue = 0x8000 | hwDuty;      // compare value; keeps running after load
  kPwm->TASKS_SEQSTART[0] = 1;
}

void adcInit() {
  analogReadResolution(12);  // note: the sketch's own analogRead() follows
}

uint16_t adcReadMv(int8_t pin) {
  return (uint16_t)(((uint32_t)analogRead(pin) * 3300UL + 2047UL) / 4095UL);
}

uint16_t senseRead(int8_t pin) {
  return (uint16_t)analogRead(pin);
}

float senseMvPerCount() {
  return 3300.0f / 4095.0f;
}

bool analogCapable(int8_t pin) {
  return pin >= A0 && pin < A0 + NUM_ANALOG_INPUTS;
}

}  // namespace MistMakerBoard

#endif
