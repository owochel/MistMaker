# MistMaker

**MistMaker** is an Arduino library for controlling piezoelectric mist maker modules using PWM, current sensing, and battery monitoring. It was developed for the custom PCBs of the [Programmable Mist Maker](https://github.com/Dav1dyang/Programmable-Mist-Maker) project, first released during the **Open Hardware Summit 2025** workshop session.

This project is [Open Source Hardware Certified](https://certification.oshwa.org/us002742.html) under OSHWA ID **US002742**, enabling educators, artists, and developers to explore the creative potential of mist as a material and medium.

---

## 🌱 Features

- 108.7 kHz PWM control for mist actuation
- **Mist dimming** — `setLevel(0..255)`, dim mist like an LED
- **Current sensing in mA** with auto or manual calibration
- **Piezo disc + water detection** — one ADC pin tells you if a disc is attached, if it fell off, and if the water ran out
- **Battery monitoring** — calibrated voltage, percent estimate, and a graceful low-battery shutdown to prevent brown-outs
- **Power-source sensing** — reads the TPS2116 mux status (Battery Kit V0.4+) so battery logic knows USB from the cell and never false-triggers
- **Pin presets for every official board variant** — one line to target your PCB
- Designed for ESP32-based boards (tested on Seeed Studio XIAO ESP32-C6)
- Modular and reusable class-based structure

> **New in v2.4:** `PhoneDemo` restores the first-boot WiFi setup portal and
> local board-button control, gives every fresh maker a private two-word room,
> and lets the phone app move makers between rooms over the air. The bundled
> Cloudflare relay and phone UI have been updated to support the new setup and
> room workflow. There are no core library API changes.

> **New in v2.3:** bare constructor accepts optional `button` / `battery` /
> `usbSense` pins; runtime `setButtonPin()` / `disableButton()` /
> `setBatteryPin()`; and `buttonPressed()` / `buttonPin()` helpers (raw
> active-HIGH level, no debounce). Existing four-pin sketches compile
> unchanged.
>
> If a sketch used the old bare-pin constructor's optional PWM arguments in
> positions 5–7, migrate it to the `MistMakerPins` constructor before upgrading:
>
> ```cpp
> MistMakerPins pins{mistPin, enPin, sensePin, ledPin, -1, -1, -1};
> MistMaker mist(pins, pwmFreq, pwmRes, dutyMax);
> ```
>
> In v2.3 those positions configure `button`, `battery`, and `usbSense`.

> **New in v2.1:** `MistMakerBatteryKitV041()` preset for the July 2026
> production board (same pins as V0.4 — the spin changed passives only), the
> `MISTMAKER_VERSION` string for banners/test reports, and `keywords.txt`
> (IDE syntax highlighting). No API changes — 2.0 sketches compile unchanged.

> **Upgrading to v2.0?**
> - `applyLevel(x)` → `setLevel(x)` (true rename, same 0..255 meaning).
> - `readCurrentVoltage()` is **removed, not renamed** — it returned the raw
>   sense-pin voltage with a legacy ×2 scale; `readCurrentMa()` returns
>   milliamps via the 3.0 V/A sense factor. Convert old thresholds with
>   `mA ≈ oldVolts × 166.7` (e.g. an old `> 0.4` check becomes `> 67` mA).
> - The constructor's last argument (duty cap) now **takes effect** — 1.x
>   accepted and ignored it. Valid caps are `1..90% of full scale` (higher
>   requests clamp to 90% — above it the drive makes heat, not mist);
>   zero/negative/omitted resolve to the 50% efficiency knee, which is exactly
>   the 1.x behavior. If an old sketch passed a valid value like `200`, it
>   will now really drive that duty — remove the argument to keep 1.x drive.
> - Everything else is source-compatible.

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
MistMaker mist(MistMakerBatteryKitV041());  // current production board (V0.4: same pins — either works)
// MistMaker mist(MistMakerBatteryKitV03()); // V0.3 board (battery sensing off — no ST pin)
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

Custom wiring? Use the pin constructor — optional trailing pins default to
`-1` (feature off); old 4-arg call sites still work:

```cpp
MistMaker mist(mistPin, enPin, sensePin, ledPin);
// MistMaker mist(mistPin, enPin, sensePin, ledPin, buttonPin);
// MistMaker mist(mistPin, enPin, sensePin, ledPin, buttonPin, battPin, usbSensePin);
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

## 🌫️ How much mist can it make? (duty cap, measured)

The PWM duty cap is the library's "how hard to drive" ceiling, and it was
bench-characterized on real V0.4 hardware (2026-07-03 duty sweep, 0→90%):

| Duty cap | What you get |
|---|---|
| **127 (50%) — the default** | The efficiency sweet spot: ~90% of practical mist at ~¼ of peak power, cool components, battery-sustainable (~0.3 A from the cell). Ships as `DUTY_AUTO`. |
| **`MistMakerDefaults::DUTY_TURBO` (178 ≈ 70%)** | The **true mist maximum** — output rises all the way to ~70% duty on this drive. Costs ~4× the input power, needs a strong 5 V supply (≥ 2 A wall adapter); a battery reaches it only seconds at full charge. |
| Above ~75% | Measurably *worse*: mist declines and turns unstable while current climbs toward 2 A — the resonant fly-back gets clipped and the energy becomes heat. The library hard-limits at 90% of full scale. |

```cpp
mist.setMaxDuty(MistMakerDefaults::DUTY_TURBO);  // wall-powered "turbo"
mist.setMaxDuty(MistMakerDefaults::DUTY_AUTO);   // back to the 50% default
```

Setting a cap doesn't change your sketch's `setLevel(0..255)` scale — 255
always means "my current maximum."

---

## 🔋 Battery Monitoring (Battery Kit)

```cpp
float v   = mist.readBatteryVolts();   // calibrated volts via the on-board divider
uint8_t p = mist.batteryPercent();     // rough LiPo gauge for UIs

if (mist.batteryCritical()) {          // hysteresis built in; false on USB
  mist.shutdown();                     // mist off + boost rail off
  esp_deep_sleep_start();              // sleep instead of brown-out
}
```

Defaults: divider ratio 2.0, low = 3.45 V, critical = 3.20 V. Override with
`setBatteryDivider()` / `setBatteryThresholds()`. Reads use the ESP32's
calibrated `analogReadMilliVolts()` (linear even on the C6).

> **Why the board can't just read `BATT+`:** the TPS2116 power mux runs the
> board off USB-C whenever it's plugged in, so `BATT+` tracks the *charger* (not
> state-of-charge) on USB — reading it blindly caused false low-battery
> shutdowns.
>
> - **Battery Kit V0.4 / V0.4.1** route the mux **ST** (status) pin to **D8**, so
>   the `MistMakerBatteryKitV041()` / `...V04()` presets self-gate: `batteryState()`
>   returns `MIST_BATT_CHARGING` on USB and only ever reports `LOW`/`CRITICAL` on
>   the cell. Use `usbPresent()` / `onBattery()` to read the source yourself.
> - **Battery Kit V0.3** has no ST pin, so its preset ships with battery
>   sensing **off** (every `battery*` call behaves as on a board with no
>   cell). `disableBattery()` remains for switching it off at runtime on
>   custom builds.

For custom wiring, pass the optional pins after the four required pins:

```cpp
MistMaker mist(mistPin, enPin, sensePin, ledPin,
               buttonPin, batteryPin, usbSensePin);
```

Use `-1` for an unavailable optional pin. For example, battery and power-source
sensing without a button is:

```cpp
MistMaker mist(mistPin, enPin, sensePin, ledPin,
               -1, batteryPin, usbSensePin);
```

See `BatteryPowerTest` for a Serial Monitor test that reports USB versus
battery power, measured voltage, estimated charge percentage, and battery
state.

---

## 📚 Examples

Seven examples are included. Hardware-specific examples identify the required
board in their comments:

| Example | What it shows |
|---|---|
| `Blink` | Hello-world: mist 6 s on / 3 s off, LED follows |
| `Breath` | Mist that breathes — smooth fade in, hold, fade out with `setLevel()` |
| `ButtonPressToMist` | Hold the Battery Kit button to mist; release it to stop |
| `ButtonOn-Off` | Debounced press-on/press-off toggle using `buttonPressed()` |
| `BatteryPowerTest` | Report USB/cell source, battery voltage, percent, and state on V0.4/V0.4.1 |
| `WaterDetect` | Self-minding mist: stops when water runs out or the disc comes off, resumes by itself; `'c'` auto-calibrates |
| `PhoneDemo` | Drive the mist from a phone's mic/light/motion/face/music via a Cloudflare relay; sync many makers ([extras/phone-app](extras/phone-app)) |

(Older examples — MQTT/Home Assistant, ESP-NOW, WiFi AP control — live in the
[v2.1.0 release](https://github.com/owochel/MistMaker/releases/tag/v2.1.0) if
you need them.)

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
   `File > Examples > MistMaker > Blink`

### 🛠 Option 2: Manual Installation

1. Clone or download this repository.
2. Move the folder into your Arduino `libraries/` directory.
3. Restart the Arduino IDE.

---

## 🧪 API Reference

Hardware defaults live in `namespace MistMakerDefaults` (top of `MistMaker.h`);
probe/calibration timing constants sit at the top of `MistMaker.cpp`.

```cpp
// --- construction ---
MistMaker(const MistMakerPins &pins, uint32_t pwmFreq = 108700,
          uint8_t pwmRes = 8,
          int dutyMax = MistMakerDefaults::DUTY_AUTO); // AUTO = 50% efficiency knee
MistMaker(int mistPin, int enPin, int sensePin, int ledPin,
          int buttonPin = -1, int battPin = -1, int usbSensePin = -1, ...);

// --- control ---
void begin();
void turnOn();  void turnOff();  void toggle();  bool isOn();
void setLevel(uint8_t level);    // 0..255 dimming
uint8_t getLevel();
void setMaxDuty(int duty);       // default 127 (50%); DUTY_TURBO=178 = measured peak
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

// --- button ---
void    setButtonPin(int8_t pin);  void disableButton();
int8_t  buttonPin();             // configured pin, or -1
bool    buttonPressed();         // active-HIGH (PCB pull-down); raw, no debounce

// --- power source (mux status, V0.4+) ---
void    setUsbSensePin(int8_t pin);                // TPS2116 ST (Battery Kit V0.4+ = D8)
bool    usbPresent();            // load on USB (mux VIN1)? true if no sense pin
bool    onBattery();             // load on the cell (mux VIN2)? = valid SoC

// --- battery ---
float   readBatteryVolts(uint8_t samples = 16);    // calibrated volts
uint8_t batteryPercent();
MistBatteryState batteryState(); // OK/LOW/CRITICAL/CHARGING, ST-gated + hysteresis
bool    batteryLow();   bool batteryCritical();
void    setBatteryDivider(float ratio);            // default 2.0
void    setBatteryThresholds(float lowV, float critV);
void    setBatteryPin(int8_t pin); // enable / re-enable sensing
void    disableBattery();        // pre-V0.4 escape hatch (turns sensing off)
void    shutdown();              // mist + boost + LED off (call before sleep)
```

---

## License & Attribution

Open-source under MIT License. Designed and tested by [shuang cai](https://shuangcai.cargo.site/) and [David Yang](https://davidyang.work/).

Current-sense classifier thresholds and ESP32-C6 ADC findings come from the
Block Kit V0.1 bench validation (2026-05).
