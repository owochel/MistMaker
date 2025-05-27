# MistMaker

**MistMaker** is an Arduino library for controlling piezoelectric mist maker modules using PWM, current sensing, and optional button toggling. It was developed for the custom PCB released during the **Open Hardware Summit 2025** workshop session.

This project is [Open Source Hardware Certified](https://certification.oshwa.org/us002742.html) under OSHWA ID **US002742**, enabling educators, artists, and developers to explore the creative potential of mist as a material and medium.

---

## 🌱 Features

- 108.7kHz PWM control for mist actuation
- Current sensing via analog input
- Toggle control using a button
- LED status indicator
- Designed for ESP32-based boards (tested on Seeed Studio XIAO ESP32-C6)
- Modular and reusable class-based structure

---

## 🧰 Hardware Requirements

- Seeed Studio XIAO ESP32-C6 (or compatible ESP32 board)
- Certified Mist Maker PCB from OHS 2025
- Piezo mist module
- Optional: Button (for manual toggle) and LED (for status)

---

## 📦 Installation

### 🔗 Option 1: Install via Arduino IDE using GitHub URL

1. In Arduino IDE, go to:  
   `Sketch > Include Library > Add .ZIP Library...`

2. Paste this URL into your browser and download the ZIP file:  
   [Download Library ZIP](https://github.com/owochel/MistMaker/archive/refs/heads/main.zip)

3. Select the downloaded ZIP file in Arduino IDE.

4. You're done! Now you can access example code via:  
   `File > Examples > MistMaker > SimpleControl`

---

### 💻 Option 2: Clone or Copy into Libraries Folder (Advanced)

Clone this subfolder manually and place it inside your Arduino libraries directory:

```bash
git clone https://github.com/owochel/MistMaker.git
cp -r MistMaker ~/Documents/Arduino/libraries/
```

Note: Your library location may be different. [Look at Arduino Support Article here](https://support.arduino.cc/hc/en-us/articles/4415103213714-Find-sketches-libraries-board-cores-and-other-files-on-your-computer) to see how you can locate your Libary folder.

## 🔐 License
   •  Hardware: CERN-OHL-P v2
   •  Code: MIT License

⸻

## 🔖 Open Source Hardware Certification

Certified by OSHWA
Certification ID: US002742

⸻

## Documentation Notes

This README and the library were written with the support of ChatGPT (GPT-4o) to:

* Summarize logs and prototyping notes
* Clarify complex circuit behavior
* Reformat documentation for aesthetic accessibility

All technical data was reviewed and validated against physical test results and datasheets.

⸻

## ✍️ Authors

Created by [David Yang](https://davidyang.work/) and [shuang cai](https://shuangcai.cargo.site/)
Released at the Open Hardware Summit 2025