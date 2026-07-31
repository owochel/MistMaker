// Seeed XIAO ESP32 (C3 / C6 / S3) — PWM via the ESP32 core's LEDC peripheral.
#if defined(ARDUINO_ARCH_ESP32)

#include "MistMakerBoard.h"

namespace {
  // ESP32 Arduino core ADC defaults (12-bit, ~3.3 V full scale). We read raw
  // counts for current sense; battery uses analogReadMilliVolts (calibrated).
  constexpr float ADC_FULL_SCALE_MV = 3300.0f;
  constexpr float ADC_MAX_COUNT     = 4095.0f;
}

namespace MistMakerBoard {

uint16_t pwmInit(int8_t pin, uint32_t freqHz, uint8_t resBits) {
  if (pin < 0) return 0;
  if (!ledcAttach(pin, freqHz, resBits)) return 0;
  ledcWrite(pin, 0);
  return (uint16_t)((1u << resBits) - 1u);
}

void pwmWrite(int8_t pin, uint16_t hwDuty) {
  ledcWrite(pin, hwDuty);
}

void adcInit() {
  // Deliberately empty: on ESP32-C6 + arduino-esp32 v3.x, analogReadResolution()
  // / analogSetPinAttenuation() have been observed to leave the ADC stuck at 0.
  // Core defaults (12-bit, ~3.3 V full scale) are what every threshold assumes.
}

uint16_t adcReadMv(int8_t pin) {
  // Factory eFuse calibration — linear millivolts even on ESP32-C6, where raw
  // analogRead() is nonlinear (arduino-esp32 #11324).
  return (uint16_t)analogReadMilliVolts(pin);
}

uint16_t senseRead(int8_t pin) {
  return (uint16_t)analogRead(pin);
}

float senseMvPerCount() {
  return ADC_FULL_SCALE_MV / ADC_MAX_COUNT;
}

bool analogCapable(int8_t pin) {
  (void)pin;
  return false;  // keeps usbPresent() on its original digitalRead path
}

}  // namespace MistMakerBoard

#endif
