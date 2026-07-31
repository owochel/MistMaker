# Lab: Making Mist With an Arduino

## Introduction

In this lab you'll drive a Programmable Mist Maker Battery Kit from an
Arduino Uno (R3 or R4) or a Nano 33 IoT using nothing but jumper wires. The
kit normally carries a Seeed XIAO on its back — but its XIAO socket is an
ordinary pair of 2.54 mm pin sockets, and with the XIAO absent those sockets
are a perfect jumper-wire header. Four wires get you mist; a few more get you
water detection, battery readouts, and a button.

By the end you'll have mist puffing on a timer, and you'll know why a mist
maker needs a very particular kind of pin to work at all.

### What You'll Need to Know

- [Digital input and output](https://itp.nyu.edu/physcomp/labs/labs-arduino-digital-and-analog/digital-input-and-output-with-an-arduino/)
- [Analog input](https://itp.nyu.edu/physcomp/labs/labs-arduino-digital-and-analog/analog-in-with-an-arduino/)
- What [PWM](https://itp.nyu.edu/physcomp/labs/labs-arduino-digital-and-analog/analog-out-with-an-arduino/) is

### Things You'll Need

- Mist Maker Battery Kit (V0.4.1) with its piezo disc, **no XIAO installed**
- An Arduino Uno R3, Uno R4 (Minima or WiFi), or Nano 33 IoT
- 4–10 male-to-male jumper wires
- A **USB wall charger rated 1 A or more** (not a laptop port — see the power
  note below), or a 9 V supply for the Uno's barrel jack
- A small container of water for the disc
- Arduino IDE with the **MistMaker** library installed (Library Manager →
  "MistMaker")

<!-- figure slot: photo of parts laid out -->
_Figure 1. The parts: the Battery Kit with its empty XIAO socket at the
center, the piezo disc on its cable, an Arduino, and a handful of jumper
wires._

## Why 108.7 kHz?

The piezo disc atomizes water only when driven at its mechanical resonance,
about 108.7 kHz. The kit has the power electronics on board — a gate driver
and MOSFET that turn a small logic signal into a strong drive — but the
timing signal comes from your Arduino.

Here's the catch: `analogWrite()` runs at about 490 Hz. That's 200× too slow —
the disc just sits there. Making 108.7 kHz takes a hardware timer, and on most
Arduinos only a few pins are attached to a timer that can do it. The MistMaker
library programs that timer for you, which is why it asks for specific pins:

| Board | Pins that can make mist |
|---|---|
| Uno R3 | 9, 10 |
| Nano 33 IoT | 5, 6, **9**, 10, 11 |
| Uno R4 | 3, 5, 6, **9**, 10, 11 |

Pin **9** works on all three boards, so this guide wires everything the same
way no matter which Arduino you have. If you pick a pin that can't do it, the
library stops you: `MISTMAKER_ASSERT_MIST_PIN` fails the compile with the
valid pins in the message, and `begin()` returns false at runtime with the
same hint on the Serial Monitor.

## Wire It Up (USB Mode)

Unplug everything first. Find the kit's empty XIAO socket — two rows of seven
holes. With the kit's USB-side row away from you, the row nearest the piezo
connector carries the signals (D0–D6) and the outer row carries power (5V,
GND, 3V3) plus D7–D10. The pads are labeled on the silkscreen.

Four wires make mist:

| # | Wire color | From Arduino | To kit pad | What it does |
|---|---|---|---|---|
| 1 | red | 5V | 5V-in | powers the kit |
| 2 | black | GND | GND | shared ground |
| 3 | yellow | 9 | D0 | the 108.7 kHz mist signal |
| 4 | green | 7 | D3 | boost enable — the kit's ~5 V drive rail is OFF until this goes HIGH |

<!-- figure slot: breadboard-style wiring view, Uno -->
_Figure 2. Uno wiring. A red wire runs from the Uno's 5V pin to the kit's
5V-in pad, and a black wire from the Uno's GND to the kit's GND pad, directly
beside it. A yellow wire runs from the Uno's pin 9 to the kit's D0 pad, and a
green wire from pin 7 to the kit's D3 pad. The piezo disc cable plugs into
the kit's disc connector._

<!-- figure slot: breadboard-style wiring view, Nano 33 IoT -->
_Figure 3. Nano 33 IoT wiring — identical pads, identical pin numbers: 5V to
5V-in (after bridging VUSB — see the note), GND to GND, pin 9 to D0, pin 7 to
D3._

### Note on Powering the Arduino

Misting draws 0.3–0.5 A through those power wires, on top of what the Arduino
uses. A laptop USB port supplies 0.5 A total and will brown out — the board
resets the moment mist starts. Use a USB **wall charger rated ≥ 1 A**, or feed
the Uno's barrel jack from a 9 V supply. And keep the mist level at its
default cap on USB power: the turbo setting (`DUTY_TURBO`) quadruples the
current draw and will sag the supply.

### Note on the Nano 33 IoT's 5V Pin

Out of the box the Nano 33 IoT's 5V pin is **not connected** — Arduino ships
it that way to protect 3.3 V projects. On the back of the board, find the
solder jumper labeled **VUSB** and bridge it with a blob of solder. After
that the 5V pin carries USB voltage and wire 1 works. (Uno R3 and R4 need no
such step.)

## Program It

Open **File → Examples → MistMaker → JumperWireQuickStart**. The whole sketch
is short. First the pin choices, with the compile-time guard:

```cpp
#include <MistMaker.h>

const int MIST_PIN = 9;  // must sit on a fast timer — the check below explains
MISTMAKER_ASSERT_MIST_PIN(MIST_PIN);

const int BOOST_ENABLE_PIN = 7;

MistMaker mist(MIST_PIN, BOOST_ENABLE_PIN, -1, -1);
```

The two `-1`s mean "no current-sense pin, no LED pin" — you haven't wired
those yet. In `setup()`, start the library and stop if the pin was wrong:

```cpp
void setup() {
  Serial.begin(115200);
  if (!mist.begin()) {
    while (true) {}  // wrong mist pin — the Serial message names the good ones
  }
}
```

The loop is the mist version of Blink:

```cpp
void loop() {
  mist.turnOn();
  delay(6000);
  mist.turnOff();
  delay(3000);
}
```

Pick your board in **Tools → Board**, upload, and set the disc in its water
container. Within a few seconds: six seconds of mist, three seconds of rest,
repeating. That's all it takes.

Try `mist.setLevel(80)` in place of `turnOn()` — mist dims like an LED, 0 to
255.

## My Mist Won't Start!

**Nothing happens at all.** Check the disc is seated in water — a dry disc
makes no visible mist. Then check wire 4 (pin 7 → D3): the kit's drive rail
boots OFF and stays off until the sketch raises that pin.

**The Arduino resets every time mist starts.** That's the power note above —
your USB source can't deliver the current. Switch to a wall charger or the
barrel jack.

**The compiler stopped with a message about the mist pin.** Good — that's the
library catching a pin that can't make 108.7 kHz. Move the mist wire and the
`MIST_PIN` constant to a pin from the table above.

**Mist worked, then got weak and erratic.** Check the water level — the disc
needs to sit in water, and low water is exactly what the water-detection
variation below is for.

## Variation: Run on Battery

The kit powers itself from its own cell — then the Arduino only supplies
signals, and the current problem disappears entirely. Wiring drops to three
wires: **GND, 9 → D0, 7 → D3**. Remove the red 5V wire, and if you added the
3.3V wire from the water-detection variation, remove that too (on battery
the kit drives its own 3.3 V rail).

This is the nicest classroom setup: any USB port can power the Arduino,
because the mist current comes from the kit's battery.

## Variation: Water Detection

The kit measures the disc's current draw, and from it can tell a disc in
water from a dry disc from no disc at all. Two more wires:

| Wire color | From Arduino | To kit pad | What it does |
|---|---|---|---|
| orange | 3.3V | 3V3 | powers the kit's sense amplifier (USB mode only) |
| blue | A1 | D2 | current-sense signal |

Change the constructor's first `-1` to `A1`, or use the full preset (below).
Then run the **WaterDetect** example: it probes the disc in every off-window,
stops when the water runs out, and resumes when you refill. Run
`autoCalibrateSense()` once (send `'c'` in the Serial Monitor) — your
Arduino's ADC differs a little from the board the default thresholds were
measured on.

## Variation: Everything Wired

For battery readouts, USB-vs-battery detection, the kit's button and LED, add
the remaining wires and use the preset — it's the same map on all three
boards:

```cpp
MistMaker mist(MistMakerBatteryKitV041());
// kit D0->9  D3->7  D6->2  D7->4  D1->A0  D2->A1  D8->A2
```

With A0 and A2 wired, the **BatteryPowerTest** example reports the power
source, battery voltage, and charge state live.

## The Uno vs the Nano 33 IoT vs the Uno R4

- **Logic levels.** The Uno R3 and R4 are 5 V boards; the Nano 33 IoT is
  3.3 V. The kit accepts both on its inputs, so the same wiring works — but
  never wire the kit's 5V-in to a Nano output pin.
- **Mist resolution.** The mist signal has 147 brightness steps on the R3 and
  442 on the Nano and R4 (their timers run faster). You'll only notice in
  slow fades at the dimmest levels.
- **Timer sharing.** On the R3, the mist borrows Timer1: the `Servo` library
  and `analogWrite()` on pins 9/10 would break the mist. On the Nano 33 IoT,
  avoid `analogWrite()` on pins 5, 6, 10, A2, A3 while misting; pins 4 and 7
  still dim LEDs fine. On the R4 there's no conflict at all.
- **Uploading.** The Nano and R4 have native USB — the port can vanish and
  reappear during upload; that's normal.

## Get Creative

Mist responds to anything a sensor can measure. Breathe on a stretch sensor
and have the mist breathe back (`Breath` example). Put mist on a doorway
with a distance sensor. Cross two misting doorways. The `setLevel()` scale
makes mist a dimmable material, like light — what does a *gesture* of mist
look like?

---

*This guide follows the format of the
[ITP Physical Computing labs](https://itp.nyu.edu/physcomp/labs/). Kit
hardware: [Programmable Mist Maker](https://github.com/Dav1dyang/Programmable-Mist-Maker),
OSHWA US002742.*
