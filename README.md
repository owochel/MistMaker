# MistMaker

**MistMaker** is an Arduino library for controlling piezoelectric mist maker modules using PWM, current sensing, and battery monitoring. It was developed for the custom PCBs of the [Programmable Mist Maker](https://github.com/Dav1dyang/Programmable-Mist-Maker) project, first released during the **Open Hardware Summit 2025** workshop session.

This project is [Open Source Hardware Certified](https://certification.oshwa.org/us002742.html) under OSHWA ID **US002742**, enabling educators, artists, and developers to explore the creative potential of mist as a material and medium.

---

## 🌱 Features

- 108.7 kHz PWM control for mist actuation
- **Mist dimming** — `setLevel(0..255)`, dim mist like an LED
- **Current sensing in mA** with auto or manual calibration
- **Piezo disc + water detection** — one ADC pin tells you if a disc is attached, if it fell off, and if the water ran out
- **Battery monitoring** — voltage, percent estimate, and a graceful low-battery shutdown to prevent brown-outs
- **Pin presets for every official board variant** — one line to target your PCB
- Designed for ESP32-based boards (tested on Seeed Studio XIAO ESP32-C6)
- Modular and reusable class-based structure; v1.0 sketches compile unchanged

---

## 🧰 Hardware Requirements

- [Seeed Studio XIAO ESP32-C6](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-Pre-Soldered-p-6328.html) (or compatible ESP32 board)
- A certified Programmable Mist Maker PCB ([all variants](https://github.com/Dav1dyang/Programmable-Mist-Maker))
- Piezo mist disc (108.7 kHz resonance)
- Optional: button, LED, LiPo battery (Battery Kit)

---

## 🚀 Quick Start

```cpp
#include <MistMaker.h>

// One line per board — pick yours:
MistMaker mist(MistMakerBatteryKitV03());
// MistMaker mist(MistMakerExtensionV01());
// MistMaker mist(MistMakerBlockKitV01());
// MistMaker mist(MistMakerLegacyV1());

void setup() {
  mist.begin();
  mist.turnOn();        // full mist
  mist.setLevel(128);   // half mist — dim it like an LED
}

void loop() {}
```

Custom wiring? Use the pin constructor (unchanged since v1.0):

```cpp
MistMaker mist(mistPin, enPin, sensePin, ledPin);
```

---

## 💧 Current Sensing, Disc & Water Detection

The boards measure piezo current through a shunt + INA180A3 current-sense
amplifier. A missing disc, a dry disc, and a disc in water each draw a
distinctly different current at a given PWM duty — so one ADC pin gives you
disc detection *and* a water-level sensor for free.

```cpp
float ma = mist.readCurrentMa();        // live current in mA

MistSenseState s = mist.probe();        // brief probe, then restores output
switch (s) {
  case MIST_WATER_OK:          /* keep misting */          break;
  case MIST_WATER_LOW:         /* warn: refill soon */     break;
  case MIST_DISC_MISSING:      /* no piezo attached */     break;
  case MIST_DISC_DISCONNECTED: /* disc fell off mid-run */ break;
}
```

**Calibration.** The library ships with bench-measured defaults (XIAO
ESP32-C6 + INA180A3 + 30 mΩ shunt). Two ways to adapt them to your build:

```cpp
// AUTO — run once with the disc attached and in water; thresholds are
// derived from what is actually measured and printed to Serial:
mist.autoCalibrateSense();

// MANUAL — hard-code values (mA), e.g. the ones auto-calibration printed:
mist.setSenseThresholds(10.0, 110.0, 70.0);
//                      ^disc  ^water ^disconnected

// Different shunt/amp? Set V-per-A factor (gain × shunt):
mist.setCurrentSenseFactor(3.0);  // INA180A3 (100 V/V) × 30 mΩ
```

---

## 🔋 Battery Monitoring (Battery Kit)

```cpp
float v   = mist.readBatteryVolts();   // via on-board divider
uint8_t p = mist.batteryPercent();     // rough LiPo gauge for UIs

if (mist.batteryCritical()) {          // hysteresis built in
  mist.shutdown();                     // mist off + boost rail off
  esp_deep_sleep_start();              // sleep instead of brown-out
}
```

Defaults: divider ratio 2.0, low = 3.45 V, critical = 3.20 V. Override with
`setBatteryDivider()` / `setBatteryThresholds()`.

> ⚠️ **Battery Kit V0.3:** the D1 divider reads `BATT+`, but the TPS2116 power
> mux means the board runs off USB-C whenever it's plugged in — so D1 can't tell
> "USB-only, no cell" from "on battery, dying," which caused false low-battery
> shutdowns. Until V0.4 adds a USB-present pin, call `mist.disableBattery();`
> before `begin()` to switch battery sensing off (every `battery*` call then
> behaves as on a board with no cell). The networked examples already do this.

---

## 📚 Examples

| Example | What it shows |
|---|---|
| `MistBlink` | Hello-world: 6 s ON / 3 s OFF cycle |
| `MistDimming` | Organic "breathing" mist with `setLevel()` |
| `WaterDetect` | Disc + water detection, auto-calibration, auto-recovery |
| `WiFiPhoneControl` | Phone control via the board's own WiFi AP + web UI |
| `PhoneSensors` | Drive the mist from a phone's mic/light/motion/face/music via a Cloudflare relay; sync many makers ([extras/phone-app](extras/phone-app)) |
| `HomeAssistant_MQTT` | Native Home Assistant device via MQTT Discovery |
| `Blink`, `SimpleControl`, `Ramping` | v1.0 basics (button toggle, level ramp) |
| `ESPNow_Control` | Show-control via ESP-NOW (no router needed) |

All examples are in `File > Examples > MistMaker` after installation.

---

## Board Compatibility

This library is designed for **ESP32-based boards** and relies on ESP32's LEDC PWM API (arduino-esp32 **v3.x**) for high-frequency mist control. Tested with:

- Seeed Studio XIAO ESP32-C6
- ESP32 DevKit V1
- ESP32-S3

❗ This library does **not support AVR-based boards** like Arduino Uno or Mega out of the box.

If you are using a different board and wish to adapt the library, you may need to:

- Replace `ledcWrite()` and `ledcAttach()` with board-specific PWM functions — and make sure the carrier stays at **108.7 kHz**, the piezo disc's resonant frequency. A plain `analogWrite()` (~490 Hz/1 kHz on AVR) will NOT make mist; on AVR you must configure a hardware timer (e.g. Timer1 fast PWM with `ICR1 = F_CPU / 108700`) to hit 108.7 kHz
- Adjust analog read scaling (12-bit vs 10-bit ADC)

> ⚠️ **ESP32-C6 ADC note:** do not call `analogReadResolution()` or
> `analogSetPinAttenuation()` with arduino-esp32 v3.x — it can leave the ADC
> stuck at 0. The library uses core defaults (12-bit, ~3.3 V full scale).

---

## 📦 Installation

### 🔗 Option 1: Install via Arduino IDE using GitHub URL

1. In Arduino IDE, go to:
   `Sketch > Include Library > Add .ZIP Library...`

2. Paste this URL into your browser and download the ZIP file:
   [Download Library ZIP](https://github.com/owochel/MistMaker/archive/refs/heads/main.zip)

3. Select the downloaded ZIP file in Arduino IDE.

4. You're done! Now you can access example code via:
   `File > Examples > MistMaker > MistBlink`

### 🛠 Option 2: Manual Installation

1. Clone or download this repository.
2. Move the folder into your Arduino `libraries/` directory.
3. Restart the Arduino IDE.

---

## 🧪 API Reference

```cpp
// --- construction ---
MistMaker(const MistMakerPins &pins, int pwmFreq = 108700,
          int pwmRes = 8, int duty = 127);
MistMaker(int mistPin, int enPin, int sensePin, int ledPin, ...); // v1.0

// --- control ---
void begin();
void turnOn();  void turnOff();  void toggle();  bool isOn();
void setLevel(uint8_t level);    // 0..255 dimming
uint8_t getLevel();
void setMaxDuty(uint8_t duty);   // default 127 (50% = resonant sweet spot)
void printStatus();

// --- current sense / detection ---
float readCurrentMa(uint16_t sampleMs = 50);
void  setCurrentSenseFactor(float voltsPerAmp);   // default 3.0
bool  autoCalibrateSense();                        // disc + water required
void  setSenseThresholds(float discMa, float waterLowMa, float disconnMa);
MistSenseState probe();          // probe + classify, restores prior output
bool  discPresent();
MistSenseState senseState();
float lastProbeMa();

// --- battery ---
float   readBatteryVolts(uint8_t samples = 16);
uint8_t batteryPercent();
MistBatteryState batteryState(); // OK / LOW / CRITICAL, with hysteresis
bool    batteryLow();   bool batteryCritical();
void    setBatteryDivider(float ratio);            // default 2.0
void    setBatteryThresholds(float lowV, float critV);
void    shutdown();              // mist + boost + LED off (call before sleep)
```

---

## License & Attribution

Open-source under MIT License. Designed and tested by [shuang cai](https://shuangcai.cargo.site/) and [David Yang](https://davidyang.work/).

Current-sense classifier thresholds and ESP32-C6 ADC findings come from the
Block Kit V0.1 bench validation (2026-05).
