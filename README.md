# Stack-chan Demonic AI Edition (M5Stack CoreS3 + Gemini Live)

An interactive, full-featured **"Underworld Demon Lord"** firmware and AI relay for **M5Stack Stack-chan (CoreS3 + K151 base)** powered by **Gemini Live**.

Built with **dual-channel USB-C + Wi-Fi transport**, **real-time demonic DSP voice**, **25 FPS Hellfire animations**, **continuous camera face-tracking & comedy roasting**, **physical IMU/Proximity/Light reflexes**, **individual 12-LED base animations**, and **8 interactive touchscreen & voice modes**.

---

## Key Features

### 1. Eight Interactive Modes (Touchscreen `[MODO >]` or Voice Command)
1. **`CARA` (`face`)** — **25 FPS Hellfire Demon Face**:
   - 10 expressions (`angry`, `inferno`, `hypnosis`, `glitch`, `laser`, `dead`, `sleepy`, `love`, `happy`, `sad`).
   - Procedural rising embers, rotating pentagrams, jagged lightning bolts, horns, and dynamic mouth lip-sync.
   - Built-in **38 kHz Infrared TV Blaster** (`GPIO 5`) triggered via `[RAYO IR]` or voice (`fire_infrared_chaos`).
2. **`VISOR` (`visor`)** — **Thermal Predator HUD**:
   - Live crimson-tinted camera feed directly on the robot's 320×240 screen with targeting reticle, motion meter (`MOV:%`), ambient light (`LUX`), and optical proximity (`PRX`).
3. **`RADAR` (`radar`)** — **Autonomous Security Sentry**:
   - Sweeps the head (`-60°..+60°`), draws a military radar sweep on screen + **12-LED Knight-Rider scanner** on the base, and sounds an intruder siren when optical motion is detected.
4. **`SYNTH` (`synth`)** — **XY Demonic Theremin & Head Puppeteer**:
   - Drag your finger across the 320×240 grid to synthesize eerie chords (`80–1280 Hz`), spawn fire trails, and physically aim the robot's pan/tilt servos in real time.
5. **`JUEGO` (`game`)** — **Soul-Crusher (*Caza-Almas*) Arcade Mini-Game**:
   - Tap the bouncing flaming skulls before they escape to harvest souls while the robot nods and Gemini mocks your score.
6. **`TAROT` (`oracle`)** — **Underworld Fortune Teller & Cursed Tarot**:
   - Tap `[TIRAR CARTA]` or ask for a reading by voice (`read_cursed_tarot`) to deal illustrated cards (*LA MUERTE XIII*, *EL DIABLO XV*, *LA TORRE XVI*, *EL COLGADO XII*, *LA RUEDA X*, *EL JUICIO XX*) while Gemini delivers a theatrical prophecy.
7. **`DEMON` (`pet`)** — **Virtual Dark Lord Tamagotchi**:
   - Live `ALMAS`, `IRA`, and `CAOS` stat bars that decay over time. Feed it souls (`[+ALMA]`), zap it with voltage (`[RAYO]`), or pet its forehead proximity sensor (`[MIMOS]`).
8. **`SCAN` (`scanner`)** — **Biometric Sin Scanner & Polygraph**:
   - Matrix-style green laser scan over the live camera feed (`scan_mortal_sins`), computing a deterministic sin percentage (`PEREZA`, `GULA`, `CODICIA`, `ENVIDIA`, `IRA`, `SOBERBIA`) and saving a *Wanted Poster* in `photos/`.

### 2. Physical Sensor Reflexes (`BMI270` IMU + `LTR-553` Proximity/Light)
- **Earthquake / Shake Reflex (`BMI270` at I2C `0x69`)**: Picking up or shaking the robot (`|a| > 1.85G`) triggers a violent glitch fit, alarm sirens, chaotic head shake, and an instant Gemini voice reaction.
- **Forehead Boop / Petting (`LTR-553` PS at I2C `0x23`)**: Placing your hand within ~4 cm of the top bezel (`PS > 600`) triggers a laser/love reaction and notifies Gemini (`[EVENTO DE SENSOR: TOQUE EN LA FRENTE]`).
- **Sudden Darkness (`LTR-553` ALS)**: Turning off the room lights (`LUX < 8`) switches the robot into spooky whispers and glowing purple/crimson abyssal eyes.

### 3. Individual 12-LED RGB Base Animations
- Custom I2C register driver (`0x30..0x53` on the K151 base MCU) controlling all 12 RGB LEDs independently:
  - **Hellfire flicker** (reds/oranges/gold), **Rainbow Rave vortex**, **Police/Intruder strobe**, **Knight-Rider scanner**, and **Audio VU-meter** synced to voice output.

### 4. Real-Time Demonic DSP & Autonomous Camera Roasting
- **Dark Lord Voice Pitch-Shifter**: Real-time sub-octave + ring-modulation + overdrive DSP in Python before streaming PCM audio to the robot.
- **Continuous Face Watchdog**: Uses OpenCV Haar Cascade on the 12 FPS camera stream to detect when a person steps in front of the robot, automatically panning the servos to center their face and prompting Gemini to roast their appearance.

---

## Project Structure

```text
demos/02.1-voice-moves/   Main Demonic AI firmware (ESP32-S3) and Python Gemini Live relay
demos/02-gemini-voice/    Shared audio codec (ES7210 mic / AW88298 speaker) & Gemini Live client
lib/StackChanHW/          Hardware drivers: SCS0009 servos, 12 RGB base LEDs, INA226, GC0308 camera
lib/StackChanLink/        Dual-transport (Wi-Fi UDP + USB-C Serial) robot <-> laptop protocol
lib/python/stackchan/     Python protocol & base relay library
start_gemini_voice.sh     Launcher script for the Demonic AI voice & vision relay
```

---

## Setup & Usage

### 1. Python Environment
```bash
python3 -m venv .venv
./.venv/bin/pip install -r requirements.txt
```

### 2. API Key (`.env`)
Create a `.env` file in the root directory (ignored by Git):
```env
GEMINI_API_KEY=your_google_ai_studio_api_key
```

### 3. Wi-Fi Configuration (`config/wifi_secrets.h`)
Create `config/wifi_secrets.h` (ignored by Git) so the robot can connect to your 2.4 GHz Wi-Fi (or rely on automatic USB-C fallback if Wi-Fi isolates clients):
```cpp
#pragma once
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
```

### 4. Build & Flash Firmware
Connect Stack-chan via USB-C and flash the firmware with PlatformIO:
```bash
./.venv/bin/pio run -d demos/02.1-voice-moves/firmware -t upload
```

### 5. Start the Demonic AI Relay
```bash
./start_gemini_voice.sh
```
