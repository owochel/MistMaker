// Arduino Uno R3 / classic Nano (ATmega328P) — PWM via 16-bit Timer1.
// Only pins 9 and 10 sit on Timer1. Servo and analogWrite(9/10) also use
// Timer1 and will break the mist signal; millis() (Timer0) and tone()
// (Timer2) are unaffected.
#if defined(ARDUINO_ARCH_AVR)

#include "MistMakerBoard.h"

#if !defined(__AVR_ATmega328P__) && !defined(__AVR_ATmega168__)
#error "MistMaker on AVR supports the ATmega328P/168 (Uno R3, classic Nano) only."
#endif

namespace MistMakerBoard {

uint16_t pwmInit(int8_t pin, uint32_t freqHz, uint8_t resBits) {
  (void)resBits;  // resolution comes from the timer top, not a bit count
  if (pin != 9 && pin != 10) return 0;
  const uint32_t top = (F_CPU + freqHz / 2) / freqHz - 1;  // 146 at 16 MHz / 108.7 kHz
  if (top < 2 || top > 65535) return 0;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  const uint8_t saved = SREG;
  cli();
  // Fast PWM, ICR1 = top (mode 14), prescaler 1. The pin stays disconnected
  // from the timer until the first nonzero duty write.
  TCCR1A = (TCCR1A & (_BV(COM1A1) | _BV(COM1B1))) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
  ICR1 = (uint16_t)top;
  if (pin == 9) OCR1A = 0; else OCR1B = 0;
  SREG = saved;
  return (uint16_t)top;
}

void pwmWrite(int8_t pin, uint16_t hwDuty) {
  const uint8_t saved = SREG;
  cli();
  if (hwDuty == 0) {
    // OCR = 0 still emits a one-cycle spike each period; disconnecting the
    // compare unit is the clean off.
    TCCR1A &= ~(pin == 9 ? _BV(COM1A1) : _BV(COM1B1));
    SREG = saved;
    digitalWrite(pin, LOW);
    return;
  }
  TCCR1A |= (pin == 9) ? _BV(COM1A1) : _BV(COM1B1);
  if (pin == 9) OCR1A = hwDuty; else OCR1B = hwDuty;
  SREG = saved;
}

void adcInit() {}

uint16_t adcReadMv(int8_t pin) {
  // The ADC reference is VCC, assumed 5.00 V; USB rails vary a few percent.
  return (uint16_t)(((uint32_t)analogRead(pin) * 5000UL + 511UL) / 1023UL);
}

uint16_t senseRead(int8_t pin) {
  return (uint16_t)analogRead(pin);
}

float senseMvPerCount() {
  return 5000.0f / 1023.0f;
}

bool analogCapable(int8_t pin) {
  return pin >= A0 && pin < A0 + NUM_ANALOG_INPUTS;
}

}  // namespace MistMakerBoard

#endif
