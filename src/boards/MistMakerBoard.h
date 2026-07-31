#ifndef MIST_MAKER_BOARD_H
#define MIST_MAKER_BOARD_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Board layer — how each chip makes the 108.7 kHz mist signal and reads its
// ADC. One implementation file per board family in this folder; MistMaker.cpp
// only ever calls these seven functions.
// ---------------------------------------------------------------------------
#if defined(ARDUINO_ARCH_ESP32)
#define MISTMAKER_MIST_PIN_HINT "On XIAO ESP32 use an output-capable GPIO (the kits use D0)."
#elif defined(ARDUINO_ARCH_AVR)
#define MISTMAKER_MIST_PIN_HINT "On Arduino Uno R3 / classic Nano use pin 9 or 10."
#elif defined(ARDUINO_ARCH_SAMD)
#define MISTMAKER_MIST_PIN_HINT "On Nano 33 IoT use pin 5, 6, 9, 10, or 11."
#elif defined(ARDUINO_ARCH_RENESAS_UNO)
#define MISTMAKER_MIST_PIN_HINT "On Uno R4 use pin 3, 5, 6, 9, 10, or 11."
#else
#error "MistMaker: this board is not supported. Supported: Seeed XIAO ESP32, Arduino Uno R3/R4, Nano 33 IoT."
#endif

namespace MistMakerBoard {

// Start the PWM carrier on `pin`. Returns the duty value that means 100%
// ("top"), or 0 if this pin can't make the frequency on this chip.
uint16_t pwmInit(int8_t pin, uint32_t freqHz, uint8_t resBits);

// Set the duty in hardware units (0..top). 0 must leave the pin at a solid LOW.
void pwmWrite(int8_t pin, uint16_t hwDuty);

// One-time ADC setup (called from MistMaker::begin()).
void adcInit();

// Calibrated millivolts on an analog pin — battery voltage and power-status.
uint16_t adcReadMv(int8_t pin);

// Current sense reads stay in raw ADC counts (summed by the caller);
// senseMvPerCount() converts an averaged count to millivolts.
uint16_t senseRead(int8_t pin);
float senseMvPerCount();

// True if adcReadMv(pin) works on this pin (used for the power-status read).
bool analogCapable(int8_t pin);

}  // namespace MistMakerBoard

#endif
