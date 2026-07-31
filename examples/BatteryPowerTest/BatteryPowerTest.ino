// MistMaker — Battery and Power Source Test
// Reports whether the board is powered by USB or battery, plus the measured
// battery voltage, estimated charge percentage, and battery state.
//
// Requires Battery Kit V0.4 or V0.4.1, whose TPS2116 status pin lets the
// library distinguish USB power from battery power. Battery Kit V0.3 cannot
// reliably identify the active power source.
//
// Boards: a Seeed XIAO ESP32 in the kit's socket, or an Arduino Uno R3/R4
// or Nano 33 IoT on jumper wires — this sketch needs the battery and ST
// wires connected (wiring: examples/JumperWireQuickStart).
// Serial Monitor: 115200 baud.

#include <MistMaker.h>

// Preset for the board revision. Pin order for manual wiring:
// MistMaker mist(mist, boostEn, sense, led, button, battery, usbSense);
MistMaker mist(MistMakerBatteryKitV041());

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
