// MistMaker — Battery and Power Source Test
// Reports whether the board is powered by USB or battery, plus the measured
// battery voltage, estimated charge percentage, and battery state.
//
// Requires Battery Kit V0.4 or V0.4.1, whose TPS2116 status pin lets the
// library distinguish USB power from battery power. Battery Kit V0.3 cannot
// reliably identify the active power source.
//
// Board: Seeed XIAO ESP32-C6 — pick "XIAO_ESP32C6" in Tools > Board.
// Serial Monitor: 115200 baud.

#include <MistMaker.h>

// Battery Kit V0.4/V0.4.1 pin map
const int MIST_OUTPUT_PIN   = D0;
const int BATTERY_PIN       = D1;
const int CURRENT_SENSE_PIN = D2;
const int EN_PIN            = D3;
const int BUTTON_PIN        = D6;
const int LED_PIN           = D7;
const int USB_SENSE_PIN     = D8;

// Recommended: use the preset for your board revision.
MistMaker mist(MistMakerBatteryKitV041());

// To assign the pins manually, comment out the line above and uncomment:
// MistMaker mist(MIST_OUTPUT_PIN, EN_PIN, CURRENT_SENSE_PIN, LED_PIN,
//                BUTTON_PIN, BATTERY_PIN, USB_SENSE_PIN);

void setup() {
  Serial.begin(115200);
  delay(1000);

  mist.begin();
  Serial.println("MistMaker battery and power-source test");
}

void loop() {
  const bool usb = mist.usbPresent();
  const float volts = mist.readBatteryVolts();
  const uint8_t percent = mist.batteryPercent();
  const MistBatteryState state = mist.batteryState();

  Serial.print("Power: ");
  Serial.print(usb ? "USB" : "BATTERY");

  Serial.print(" | Battery: ");
  Serial.print(volts, 2);
  Serial.print(" V");

  Serial.print(" | Estimated charge: ");
  Serial.print(percent);
  Serial.print("%");

  Serial.print(" | State: ");
  switch (state) {
    case MIST_BATT_OK:
      Serial.print("OK");
      break;
    case MIST_BATT_LOW:
      Serial.print("LOW");
      break;
    case MIST_BATT_CRITICAL:
      Serial.print("CRITICAL");
      break;
    case MIST_BATT_CHARGING:
      Serial.print("CHARGING");
      break;
    default:
      Serial.print("UNKNOWN");
      break;
  }

  Serial.println();
  delay(1000);
}
