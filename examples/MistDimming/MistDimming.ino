// MistDimming — smooth "breathing" mist.
//
// Demonstrates setLevel(0..255): the mist output is dimmable just like an
// LED. Level maps onto PWM duty (capped at 50% duty, the disc's resonant
// sweet spot), so 255 = full mist, 128 = half, 0 = off.
//
// This sketch breathes the mist with a sine wave — a slow swell up and down,
// like the Block Kit's wave mode.
//
// Board: Seeed XIAO ESP32-C6 (select XIAO_ESP32C6 in Tools > Board)
// Library: MistMaker >= 1.1.0

#include <MistMaker.h>
#include <math.h>

// ---- Select your board (uncomment exactly ONE) ----
MistMaker mist(MistMakerBatteryKitV03());
// MistMaker mist(MistMakerExtensionV01());
// MistMaker mist(MistMakerBlockKitV01());
// MistMaker mist(MistMakerLegacyV1());

const unsigned long BREATH_PERIOD_MS = 8000; // one full swell
const uint8_t LEVEL_FLOOR = 30;              // don't go fully off mid-breath

void setup() {
  Serial.begin(115200);
  delay(1000);
  mist.begin();
  Serial.println("MistDimming: sine breathing, period 8 s");
}

void loop() {
  // Phase 0..2pi over BREATH_PERIOD_MS
  const float phase = (millis() % BREATH_PERIOD_MS) * (2.0f * PI / BREATH_PERIOD_MS);
  // exp(sin) breathing looks more organic than a plain sine
  const float breath = (exp(sin(phase)) - 0.3679f) * (1.0f / 2.3504f); // 0..1
  const uint8_t level = LEVEL_FLOOR + (uint8_t)((255 - LEVEL_FLOOR) * breath);

  mist.setLevel(level);

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    Serial.print("level: ");
    Serial.println(level);
  }
  delay(20);
}
