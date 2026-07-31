// MistMaker — Jumper Wire Quick Start
// First mist from an Arduino Uno (R3 or R4) or Nano 33 IoT, wired to the
// Mist Maker Battery Kit with jumper wires. 6 seconds on, 3 seconds off.
//
// Full guide: docs/lab-jumper-wire-mist.md
//
// USB MODE (default) — 4 wires into the kit's empty XIAO socket:
//   Arduino 5V  -> kit 5V pad   (socket outer row, corner pin)
//   Arduino GND -> kit GND pad
//   Arduino 9   -> kit D0 pad   (mist signal)
//   Arduino 7   -> kit D3 pad   (boost enable)
// Power the Arduino from a >= 1 A USB wall charger or the barrel jack —
// misting draws up to 0.5 A and a laptop port will brown out.
// Nano 33 IoT only: its 5V pin is dead until the VUSB solder jumper on the
// back of the board is bridged.
//
// Optional water detection, +2 wires (then try the WaterDetect example):
//   Arduino 3.3V -> kit 3V3 pad      Arduino A1 -> kit D2 pad
//
// BATTERY MODE — kit runs on its own cell, 3 wires:
//   GND -> GND, 9 -> D0, 7 -> D3. Remove the 3.3V wire in battery mode.
//
// Full pin map (what the MistMakerBatteryKitV041 preset uses):
//   kit D0 -> 9    kit D3 -> 7    kit D6 -> 2    kit D7 -> 4
//   kit D1 -> A0   kit D2 -> A1   kit D8 -> A2   GND <-> GND

#include <MistMaker.h>

const int MIST_PIN = 9;  // must sit on a fast timer — the check below explains
MISTMAKER_ASSERT_MIST_PIN(MIST_PIN);

const int BOOST_ENABLE_PIN = 7;

MistMaker mist(MIST_PIN, BOOST_ENABLE_PIN, -1, -1);

void setup() {
  Serial.begin(115200);
  if (!mist.begin()) {
    while (true) {}  // wrong mist pin — the Serial message names the good ones
  }
}

void loop() {
  mist.turnOn();
  delay(6000);
  mist.turnOff();
  delay(3000);
}
