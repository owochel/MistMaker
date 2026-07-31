// Arduino Nano 33 IoT (SAMD21) — PWM via a TCC timer clocked at 48 MHz.
// TCC-backed pins: 5, 6, 9, 10 (TCC0) and 11 (TCC2). All channels of one TCC
// share the period, so analogWrite() on another TCC0 pin while misting would
// wreck the mist frequency — dim LEDs on pin 4 or 7 (TCC1) instead.
#if defined(ARDUINO_ARCH_SAMD)

#include "MistMakerBoard.h"
#include "wiring_private.h"

namespace {
  void syncTCC(Tcc* tcc) {
    while (tcc->SYNCBUSY.reg & TCC_SYNCBUSY_MASK) {}
  }
}

namespace MistMakerBoard {

uint16_t pwmInit(int8_t pin, uint32_t freqHz, uint8_t resBits) {
  (void)resBits;  // resolution comes from the timer top, not a bit count
  if (pin < 0 || (uint8_t)pin >= PINS_COUNT) return 0;
  const PinDescription desc = g_APinDescription[pin];
  // Only TCC timers take a custom period; TC-backed pins (2, 3, 12) can't.
  if (!(desc.ulPinAttribute & PIN_ATTR_PWM)) return 0;
  const uint32_t tccNum = GetTCNumber(desc.ulPWMChannel);
  if (tccNum >= TCC_INST_NUM) return 0;
  const uint32_t top = (VARIANT_MCK + freqHz / 2) / freqHz - 1;  // 441 at 48 MHz
  if (top < 2 || top > 0xFFFF) return 0;

  pinPeripheral(pin, (desc.ulPinAttribute & PIN_ATTR_TIMER) ? PIO_TIMER
                                                            : PIO_TIMER_ALT);

  // Same bring-up as the core's analogWrite, but with our period, not 0xFFFF.
  Tcc* tcc = (Tcc*)GetTC(desc.ulPWMChannel);
  const uint8_t ch = GetTCChannelNumber(desc.ulPWMChannel);
  GCLK->CLKCTRL.reg = (uint16_t)(GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 |
      GCLK_CLKCTRL_ID(tccNum == 2 ? GCM_TCC2_TC3 : GCM_TCC0_TCC1));
  while (GCLK->STATUS.bit.SYNCBUSY) {}
  tcc->CTRLA.bit.ENABLE = 0;              syncTCC(tcc);
  // Pin the prescaler to DIV1 — the period math assumes an undivided 48 MHz.
  tcc->CTRLA.reg = (tcc->CTRLA.reg & ~TCC_CTRLA_PRESCALER_Msk) |
                   TCC_CTRLA_PRESCALER_DIV1;
  tcc->WAVE.reg = TCC_WAVE_WAVEGEN_NPWM;  syncTCC(tcc);
  tcc->PER.reg = top;                     syncTCC(tcc);
  tcc->CC[ch].reg = 0;                    syncTCC(tcc);
  tcc->CTRLA.bit.ENABLE = 1;              syncTCC(tcc);
  return (uint16_t)top;
}

void pwmWrite(int8_t pin, uint16_t hwDuty) {
  const PinDescription desc = g_APinDescription[pin];
  Tcc* tcc = (Tcc*)GetTC(desc.ulPWMChannel);
  const uint8_t ch = GetTCChannelNumber(desc.ulPWMChannel);
  // Buffered update: the new duty lands cleanly at the next period boundary.
  tcc->CTRLBSET.bit.LUPD = 1;  syncTCC(tcc);
  tcc->CCB[ch].reg = hwDuty;   syncTCC(tcc);
  tcc->CTRLBCLR.bit.LUPD = 1;  syncTCC(tcc);
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
  if (pin < 0 || (uint8_t)pin >= PINS_COUNT) return false;
  return g_APinDescription[pin].ulADCChannelNumber != No_ADC_Channel;
}

}  // namespace MistMakerBoard

#endif
