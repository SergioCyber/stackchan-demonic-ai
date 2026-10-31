// Demo 02.1 - Head and moods by voice. Demo 02's microphone and speaker over UDP
// (audio.cpp, compiled in from demo 02), without the VR viewer: the relay moves the
// head with POSE packets and changes the face with "face <mood>".
// Hands-free by default: just talk. Tap the screen to mute/unmute the mic.
// The hardware and the UDP link live in lib/ (StackChanHW, StackChanLink).
#include <M5Unified.h>
#include <WiFi.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include "link_config.h"
#include "board.h"
#include "camera.h"
#include "link.h"
#include "audio.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "Missing config/wifi_secrets.h: run ./setup.sh --wifi (or setup.bat -Wifi on Windows)"
#endif

static M5Canvas status(&M5.Display);
static M5Canvas face(&M5.Display);
static bool s_linkStarted = false;
#ifdef WIFI_SSID_2
static const char* const s_ssids[] = { WIFI_SSID, WIFI_SSID_2 };
static const int s_numSsids = 2;
#else
static const char* const s_ssids[] = { WIFI_SSID };
static const int s_numSsids = 1;
#endif
static int s_ssidIdx = 0;
static uint32_t s_wifiAttemptMs = 0;

// --- Faces, Special Demonic Modes & Hellfire Animation ---
enum Mood { NEUTRAL, HAPPY, SAD, SURPRISED, ANGRY, SLEEPY, INFERNO, HYPNOSIS, GLITCH, LASER, MOOD_COUNT };
static const char* const MOOD_NAMES[MOOD_COUNT] = {
    "neutral", "happy", "sad", "surprised", "angry", "sleepy",
    "inferno", "hypnosis", "glitch", "laser"
};
static volatile int s_wantMood = ANGRY;     // start with demonic expression by default
static int s_mood = ANGRY;
static volatile bool s_wantIrZap = false;
static constexpr uint8_t IR_TX_PIN = 5;

static void irMark(uint32_t us) {
    uint32_t t0 = micros();
    while ((uint32_t)(micros() - t0) < us) {
        digitalWrite(IR_TX_PIN, HIGH);
        delayMicroseconds(13);
        digitalWrite(IR_TX_PIN, LOW);
        delayMicroseconds(13);
    }
}

static void irSpace(uint32_t us) {
    digitalWrite(IR_TX_PIN, LOW);
    if (us > 0) delayMicroseconds(us);
}

static void irSendFrame(uint32_t data, uint32_t leadMark, uint32_t leadSpace) {
    irMark(leadMark);
    irSpace(leadSpace);
    for (int i = 31; i >= 0; i--) {
        irMark(560);
        irSpace((data & (1UL << i)) ? 1690 : 560);
    }
    irMark(560);
    irSpace(0);
}

static void fireIrBeam() {
    pinMode(IR_TX_PIN, OUTPUT);
    // Universal TV power codes: Samsung (0xE0E040BF) + LG/NEC (0x20DF10EF)
    irSendFrame(0xE0E040BFUL, 4500, 4500);
    delay(40);
    irSendFrame(0x20DF10EFUL, 9000, 4500);
}

struct Ember {
    float x, y, vx, vy;
    uint8_t size;
    uint8_t heat; // 0..255
};
static constexpr int NUM_EMBERS = 36;
static Ember s_embers[NUM_EMBERS];
static bool s_embersInit = false;

static void initEmbers() {
    for (int i = 0; i < NUM_EMBERS; i++) {
        s_embers[i].x = random(8, 312);
        s_embers[i].y = random(20, 195);
        s_embers[i].vx = (random(-40, 41)) * 0.02f;
        s_embers[i].vy = -0.8f - random(0, 100) * 0.025f;
        s_embers[i].size = random(1, 4);
        s_embers[i].heat = random(80, 255);
    }
    s_embersInit = true;
}

static void drawPentagram(int cx, int cy, int r, float angleOffset, uint16_t col) {
    int px[5], py[5];
    for (int i = 0; i < 5; i++) {
        float a = angleOffset + i * (2.0f * (float)M_PI / 5.0f);
        px[i] = cx + (int)(cosf(a) * r);
        py[i] = cy + (int)(sinf(a) * r);
    }
    face.drawCircle(cx, cy, r + 2, TFT_RED);
    for (int i = 0; i < 5; i++) {
        int next = (i + 2) % 5;
        face.drawWideLine(px[i], py[i], px[next], py[next], 2, col);
    }
}

static void drawHellfireFrame(int mood, int mouth, uint32_t now) {
    if (!s_embersInit) initEmbers();
    const float t = now * 0.001f;
    const bool speaking = (mouth == 2);
    const bool listening = (mouth == 1);
    const bool isInferno = (mood == INFERNO);
    const bool isGlitch  = (mood == GLITCH);

    // Dark abyssal background gradient at the bottom
    face.fillSprite(TFT_BLACK);
    for (int y = isInferno ? 80 : 148; y < 196; y += 4) {
        uint8_t r = min(255, (y - (isInferno ? 80 : 148)) * 2);
        face.fillRect(0, y, 320, 4, face.color565(r, 0, 0));
    }

    // 1. Background & side hellfire tongues (much taller in INFERNO or when speaking)
    const int flameBaseH = isInferno ? 95 : (speaking ? 46 : 26);
    for (int i = 0; i < 32; i++) {
        int cx = i * 10 + 5;
        int edgeBoost = (cx < 55 || cx > 265) ? 45 : 0;
        float wave = sinf(t * 7.5f + i * 0.9f) * 10.0f + cosf(t * 12.0f - i * 1.4f) * 7.0f;
        int h = flameBaseH + edgeBoost + (int)wave + (random(0, 9));
        int topY = max(4, 196 - h);
        int sway = (int)(sinf(t * 6.0f + i * 0.7f) * 6.0f);
        face.fillTriangle(cx - 11, 196, cx + 11, 196, cx + sway, topY, TFT_MAROON);
        face.fillTriangle(cx - 8, 196, cx + 8, 196, cx + sway, topY + 7, TFT_RED);
        face.fillTriangle(cx - 5, 196, cx + 5, 196, cx + sway / 2, topY + 15, TFT_ORANGE);
        face.fillTriangle(cx - 3, 196, cx + 3, 196, cx, topY + 23, TFT_YELLOW);
    }

    // 2. Floating rising embers / sparks
    for (int i = 0; i < NUM_EMBERS; i++) {
        Ember& e = s_embers[i];
        e.x += e.vx + sinf(t * 5.0f + i) * 0.4f;
        e.y += e.vy * ((speaking || isInferno) ? 1.9f : 1.0f);
        if (e.heat > 3) e.heat -= 3;
        if (e.y < 4 || e.heat <= 10) {
            e.x = random(6, 314);
            e.y = 190 + random(0, 6);
            e.vx = (random(-50, 51)) * 0.02f;
            e.vy = -1.0f - random(0, 100) * 0.03f;
            e.size = random(1, 4);
            e.heat = 255;
        }
        uint16_t col = (e.heat > 200) ? TFT_YELLOW : (e.heat > 120) ? TFT_ORANGE : TFT_RED;
        face.fillCircle((int)e.x, (int)e.y, e.size, col);
    }

    // 3. Curved Demon Horns & Forehead Sigil / Rotating Pentagram
    face.fillTriangle(36, 52, 76, 48, 22, 6, TFT_MAROON);
    face.fillTriangle(44, 50, 72, 48, 28, 12, TFT_RED);
    face.fillTriangle(52, 48, 66, 48, 36, 22, TFT_ORANGE);

    face.fillTriangle(284, 52, 244, 48, 298, 6, TFT_MAROON);
    face.fillTriangle(276, 50, 248, 48, 292, 12, TFT_RED);
    face.fillTriangle(268, 48, 254, 48, 284, 22, TFT_ORANGE);

    if (isInferno || mood == HYPNOSIS) {
        drawPentagram(160, 32, 20, t * 2.8f, TFT_YELLOW);
        // Crackling lightning bolts in INFERNO mode
        if (isInferno && (random(0, 100) < 65)) {
            int lx = random(40, 280);
            face.drawWideLine(lx, 0, lx + random(-20, 21), 35, 2, TFT_YELLOW);
            face.drawWideLine(lx + random(-20, 21), 35, lx + random(-30, 31), 75, 2, TFT_WHITE);
        }
    } else {
        int pulse = (int)(sinf(t * 6.0f) * 3.0f);
        face.drawCircle(160, 30, 14 + pulse / 2, TFT_MAROON);
        face.fillTriangle(160, 13 - pulse, 149, 30, 171, 30, TFT_RED);
        face.fillTriangle(160, 47 + pulse, 149, 30, 171, 30, TFT_RED);
        face.fillCircle(160, 30, 6, TFT_ORANGE);
        face.fillCircle(160, 30, 3, TFT_YELLOW);
        face.fillRect(159, 25, 3, 11, TFT_BLACK);
    }

    // 4. Animated Demonic Eyes
    const int lookShiftX = isGlitch ? random(-10, 11) : (int)(sinf(t * 1.9f) * 3.0f);
    const int lookShiftY = isGlitch ? random(-6, 7) : (int)(cosf(t * 2.5f) * 2.0f);
    const bool blinking = (!isInferno && !isGlitch && mood != LASER && mood != HYPNOSIS && ((now % 4600) < 110));

    for (int side = 0; side < 2; side++) {
        const int x = (side ? 224 : 96) + (isGlitch ? random(-6, 7) : 0);
        const int y = 82 + (isGlitch ? random(-5, 6) : 0);
        const int in = side ? -1 : 1;
        if (blinking) {
            face.fillRect(x - 22, y + 2, 44, 5, TFT_RED);
            face.fillRect(x - 14, y + 3, 28, 3, TFT_YELLOW);
            continue;
        }
        switch (mood) {
        case HAPPY:
            face.fillArc(x, y + 10, 11, 25, 180, 360, TFT_RED);
            face.fillArc(x, y + 10, 14, 21, 195, 345, TFT_ORANGE);
            face.fillArc(x, y + 10, 16, 19, 215, 325, TFT_YELLOW);
            face.drawWideLine(x - 26 * in, y - 24, x + 22 * in, y - 10, 4, TFT_RED);
            break;
        case SAD: {
            face.fillCircle(x, y + 4, 19, TFT_MAROON);
            face.fillCircle(x, y + 4, 13, TFT_RED);
            face.fillRect(x - 2 + lookShiftX, y - 4, 5, 16, TFT_BLACK);
            int tearY = y + 20 + ((now / 18 + side * 15) % 32);
            face.fillTriangle(x - 5, tearY, x + 5, tearY, x, tearY - 9, TFT_RED);
            face.fillCircle(x, tearY + 2, 4, TFT_RED);
            face.drawWideLine(x - 22 * in, y - 12, x + 20 * in, y - 24, 4, TFT_RED);
            break;
        }
        case HYPNOSIS: {
            // Spinning multicolor hypnotic vortex eyes
            const uint16_t cols[4] = { TFT_MAGENTA, TFT_CYAN, TFT_YELLOW, TFT_RED };
            int phase = (now / 50) + (side * 2);
            for (int ring = 0; ring < 4; ring++) {
                int rad = 26 - ring * 6;
                face.fillCircle(x, y, rad, cols[(ring + phase) & 3]);
            }
            float a = t * (side ? -8.0f : 8.0f);
            face.drawWideLine(x, y, x + (int)(cosf(a) * 24), y + (int)(sinf(a) * 24), 3, TFT_BLACK);
            break;
        }
        case SURPRISED: {
            int rPulse = (now / 60) % 6;
            face.fillCircle(x, y, 25, TFT_RED);
            face.fillCircle(x, y, 18 - (rPulse / 2), TFT_ORANGE);
            face.fillCircle(x, y, 11 - (rPulse / 3), TFT_YELLOW);
            face.fillCircle(x + lookShiftX, y + lookShiftY, 4, TFT_BLACK);
            break;
        }
        case SLEEPY:
            face.fillRect(x - 22, y + 2, 44, 8, TFT_MAROON);
            face.fillRect(x - 16, y + 4, 32, 4, TFT_RED);
            face.fillCircle(x + lookShiftX, y + 5, 4, TFT_YELLOW);
            break;
        case INFERNO:
        case LASER:
        case GLITCH:
        case ANGRY:
        default: {
            int eyeFlicker = random(-1, 2);
            face.fillCircle(x, y + 4, 22 + eyeFlicker, TFT_MAROON);
            face.fillCircle(x, y + 4, 19, TFT_RED);
            face.fillCircle(x, y + 4, 13, TFT_ORANGE);
            face.fillCircle(x, y + 4, 8, (isInferno || mood == LASER) ? TFT_WHITE : TFT_YELLOW);
            int px = x + lookShiftX;
            face.fillTriangle(px, y - 12, px - 4, y + 4, px + 4, y + 4, TFT_BLACK);
            face.fillTriangle(px, y + 20, px - 4, y + 4, px + 4, y + 4, TFT_BLACK);
            face.fillCircle(px - 5 * in, y - 2, 2, TFT_WHITE);
            face.fillTriangle(x - 30 * in, y - 22, x + 25 * in, y - 3, x + 25 * in, y - 19, TFT_BLACK);
            face.drawWideLine(x - 28 * in, y - 24, x + 24 * in, y - 5, 5, TFT_RED);
            face.drawWideLine(x - 26 * in, y - 26, x + 22 * in, y - 7, 2, TFT_ORANGE);
            if (mood == LASER) {
                // Twin high-energy laser beams blasting out of the eyes!
                int tx = side ? 295 : 25;
                int ty = 192;
                face.drawWideLine(x, y + 4, tx, ty, 12, TFT_RED);
                face.drawWideLine(x, y + 4, tx, ty, 7, TFT_ORANGE);
                face.drawWideLine(x, y + 4, tx, ty, 3, TFT_YELLOW);
                face.fillCircle(tx + random(-6, 7), ty - random(0, 8), random(4, 10), TFT_YELLOW);
            }
            break;
        }
        }
    }

    // Glitch scanline bars in GLITCH mode
    if (isGlitch) {
        for (int g = 0; g < 5; g++) {
            int gy = random(8, 185);
            face.fillRect(0, gy, 320, random(2, 6), (g & 1) ? TFT_RED : TFT_YELLOW);
        }
    }

    // 5. Animated Fanged Demon Mouth
    if (listening) {
        int r = 13 + (int)(sinf(t * 10.0f) * 3.0f);
        face.fillCircle(160, 146, r + 4, TFT_MAROON);
        face.drawCircle(160, 146, r + 2, TFT_RED);
        face.drawCircle(160, 146, r - 2, TFT_ORANGE);
        face.fillTriangle(147, 132, 153, 132, 150, 143, TFT_WHITE);
        face.fillTriangle(167, 132, 173, 132, 170, 143, TFT_WHITE);
    } else if (speaking || isInferno || isGlitch) {
        int jawOpen = 14 + (int)(fabsf(sinf(t * 18.0f)) * 14.0f) + random(0, 4);
        int cy = 148 + (isGlitch ? random(-4, 5) : 0);
        face.fillEllipse(160, cy, 58, jawOpen + 4, TFT_MAROON);
        face.drawEllipse(160, cy, 58, jawOpen + 4, TFT_RED);
        face.fillEllipse(160, cy + 3, 40, max(6, jawOpen - 4), TFT_RED);
        face.fillEllipse(160, cy + 5, 26, max(4, jawOpen - 8), TFT_ORANGE);
        face.fillEllipse(160, cy + 6, 14, max(2, jawOpen - 12), TFT_YELLOW);

        int topLip = cy - jawOpen + 2;
        int botLip = cy + jawOpen - 2;
        face.fillTriangle(112, topLip + 4, 124, topLip + 2, 118, topLip + 22, TFT_WHITE);
        face.fillTriangle(132, topLip,     142, topLip,     137, topLip + 14, TFT_WHITE);
        face.fillTriangle(154, topLip - 1, 166, topLip - 1, 160, topLip + 13, TFT_WHITE);
        face.fillTriangle(178, topLip,     188, topLip,     183, topLip + 14, TFT_WHITE);
        face.fillTriangle(196, topLip + 2, 208, topLip + 4, 202, topLip + 22, TFT_WHITE);

        face.fillTriangle(124, botLip - 2, 134, botLip - 2, 129, botLip - 15, TFT_WHITE);
        face.fillTriangle(146, botLip,     156, botLip,     151, botLip - 12, TFT_WHITE);
        face.fillTriangle(164, botLip,     174, botLip,     169, botLip - 12, TFT_WHITE);
        face.fillTriangle(186, botLip - 2, 196, botLip - 2, 191, botLip - 15, TFT_WHITE);
    } else {
        int breath = (int)(sinf(t * 4.0f) * 2.0f);
        if (mood == HAPPY) {
            face.fillArc(160, 120, 42, 50, 20, 160, TFT_RED);
            face.fillTriangle(124, 140, 134, 143, 127, 160 + breath, TFT_WHITE);
            face.fillTriangle(144, 148, 154, 150, 149, 162 + breath, TFT_WHITE);
            face.fillTriangle(166, 150, 176, 148, 171, 162 + breath, TFT_WHITE);
            face.fillTriangle(186, 143, 196, 140, 193, 160 + breath, TFT_WHITE);
        } else {
            face.drawWideLine(106, 146 + breath, 135, 139, 4, TFT_RED);
            face.drawWideLine(135, 139, 160, 146 + breath, 4, TFT_RED);
            face.drawWideLine(160, 146 + breath, 185, 139, 4, TFT_RED);
            face.drawWideLine(185, 139, 214, 146 + breath, 4, TFT_RED);
            face.fillTriangle(114, 143, 126, 141, 119, 164 + breath, TFT_WHITE);
            face.fillTriangle(139, 141, 149, 143, 144, 155 + breath, TFT_WHITE);
            face.fillTriangle(171, 143, 181, 141, 176, 155 + breath, TFT_WHITE);
            face.fillTriangle(194, 141, 206, 143, 201, 164 + breath, TFT_WHITE);
        }
    }

    face.pushSprite(0, 0);
}

// --- Operating Modes (Interactive Multi-Mode System) ---
enum OpMode { OP_FACE = 0, OP_VISOR, OP_RADAR, OP_SYNTH, OP_GAME, OP_ORACLE, OP_PET, OP_SCANNER, OP_COUNT };
static const char* const OP_NAMES[OP_COUNT]  = { "face", "visor", "radar", "synth", "game", "oracle", "pet", "scanner" };
static const char* const OP_LABELS[OP_COUNT] = { "CARA", "VISOR", "RADAR", "SYNTH", "JUEGO", "TAROT", "DEMON", "SCAN" };
static volatile int s_opMode = OP_FACE;
static constexpr uint8_t T_ROBOT_EVENT = 0x28;

static void sendRobotEvent(const char* evt) {
    linkSend(T_ROBOT_EVENT, (const uint8_t*)evt, strlen(evt));
}

// --- Hardware Sensors: LTR-553 (Light & Proximity, 0x23) + BMI270 IMU (Shake & Tilt) ---
static constexpr uint8_t LTR553_ADDR = 0x23;
static float s_lux = 50.0f;
static int   s_prox = 0;
static float s_gForce = 1.0f;
static float s_gyroMag = 0.0f;
static float s_rollDeg = 0.0f;
static uint32_t s_raveUntilMs = 0;

static void ltrInit() {
    I2CGuard lock;
    M5.In_I2C.writeRegister8(LTR553_ADDR, 0x80, 0x01, 400000); // ALS active, gain 1x
    M5.In_I2C.writeRegister8(LTR553_ADDR, 0x81, 0x03, 400000); // PS active
    M5.In_I2C.writeRegister8(LTR553_ADDR, 0x82, 0x7F, 400000); // LED 60 kHz, 100 mA
    M5.In_I2C.writeRegister8(LTR553_ADDR, 0x84, 0x02, 400000); // PS 100 ms
}

static void pollHardwareSensors(uint32_t now) {
    static uint32_t lastSensMs = 0;
    if (now - lastSensMs < 100) return;
    lastSensMs = now;

    uint8_t b[4];
    if (M5.In_I2C.readRegister(LTR553_ADDR, 0x88, b, 4, 400000)) {
        float ch1 = b[0] | (b[1] << 8), ch0 = b[2] | (b[3] << 8);
        float ratio = (ch0 + ch1 > 0) ? (ch1 / (ch0 + ch1)) : 0;
        if (ratio < 0.45f) s_lux = 1.7743f * ch0 + 1.1059f * ch1;
        else if (ratio < 0.64f) s_lux = 4.2785f * ch0 - 1.9548f * ch1;
        else if (ratio < 0.85f) s_lux = 0.5926f * ch0 + 0.1185f * ch1;
        else s_lux = 0.0f;
    }
    if (M5.In_I2C.readRegister(LTR553_ADDR, 0x8D, b, 2, 400000)) {
        s_prox = (b[0] | (b[1] << 8)) & 0x7FF;
    }

    if (M5.Imu.update()) {
        float ax, ay, az, gx, gy, gz;
        M5.Imu.getAccel(&ax, &ay, &az);
        M5.Imu.getGyro(&gx, &gy, &gz);
        s_gForce = sqrtf(ax * ax + ay * ay + az * az);
        s_gyroMag = sqrtf(gx * gx + gy * gy + gz * gz);
        s_rollDeg = atan2f(-ax, ay) * (180.0f / (float)M_PI);
    }

    // 1. Shake / Earthquake Reflex (picking up and shaking the robot)
    static uint32_t lastShakeMs = 0;
    if ((s_gForce > 2.15f || s_gyroMag > 320.0f) && now - lastShakeMs > 8000) {
        lastShakeMs = now;
        s_wantMood = GLITCH;
        audioBeep(420, 120);
        sendRobotEvent("shake");
    }

    // 2. Forehead Proximity / "Boop" Reflex (hand within ~3 cm of camera/forehead)
    static uint32_t lastBoopMs = 0;
    if (s_prox > 450 && now - lastBoopMs > 7000) {
        lastBoopMs = now;
        if (s_opMode == OP_PET) {
            audioBeep(1400, 70);
            sendRobotEvent("pet:love");
        } else {
            s_wantMood = LASER;
            s_wantIrZap = true;
            audioBeep(1850, 80);
            servoSetTarget(random(-25, 26) * (float)M_PI / 180.0f, 32.0f * (float)M_PI / 180.0f);
            sendRobotEvent("boop");
        }
    }

    // 3. Sudden Darkness Reflex (lights turned off or sensor covered)
    static bool wasDark = false;
    static uint32_t lastDarkMs = 0;
    bool isDark = (s_lux < 2.5f && s_prox < 250);
    if (isDark && !wasDark && now - lastDarkMs > 15000) {
        lastDarkMs = now;
        s_wantMood = INFERNO;
        audioBeep(310, 110);
        sendRobotEvent("darkness");
    }
    wasDark = isDark;
}

// --- Dynamic 12-LED Base Animations (Fire Wave, Cylon Scanner, Mystical Vortex, Rave) ---
static void animateBaseLeds(uint32_t now) {
    uint8_t leds[12][3];
    if (now < s_raveUntilMs) {
        // High-speed cyber-demon rave strobe
        for (int i = 0; i < 12; i++) {
            int phase = (i + now / 50) % 6;
            leds[i][0] = (phase < 2 || phase == 5) ? 255 : 0;
            leds[i][1] = (phase >= 1 && phase <= 3) ? 200 : 0;
            leds[i][2] = (phase >= 3) ? 255 : 0;
        }
        setBaseLedsIndividual(leds);
        return;
    }
    if (s_opMode == OP_RADAR) {
        // Knight-Rider / Cylon red scanner sweeping across the 12 LEDs
        int pos = (now / 70) % 22;
        if (pos >= 11) pos = 22 - pos;
        for (int i = 0; i < 12; i++) {
            int d = abs(i - pos);
            leds[i][0] = d == 0 ? 255 : (d == 1 ? 110 : (d == 2 ? 35 : 5));
            leds[i][1] = d == 0 ? 40 : 0;
            leds[i][2] = 0;
        }
        setBaseLedsIndividual(leds);
    } else if (s_opMode == OP_ORACLE || (s_opMode == OP_FACE && s_mood == HYPNOSIS)) {
        // Mystical rotating purple-cyan-gold vortex
        for (int i = 0; i < 12; i++) {
            float w = sinf(now * 0.006f + i * 0.52f) * 0.5f + 0.5f;
            leds[i][0] = (uint8_t)(80 + w * 175);
            leds[i][1] = (uint8_t)((1.0f - w) * 90);
            leds[i][2] = (uint8_t)(140 + w * 115);
        }
        setBaseLedsIndividual(leds);
    } else if (s_opMode == OP_SCANNER) {
        // Biometric emerald + crimson pulse
        for (int i = 0; i < 12; i++) {
            bool odd = ((i + now / 120) & 1);
            leds[i][0] = odd ? 220 : 0;
            leds[i][1] = odd ? 0 : 230;
            leds[i][2] = 30;
        }
        setBaseLedsIndividual(leds);
    } else if (audioSpeaking() || (s_opMode == OP_FACE && s_mood == INFERNO)) {
        // Flickering hellfire across all 12 individual LEDs
        for (int i = 0; i < 12; i++) {
            leds[i][0] = random(160, 256);
            leds[i][1] = random(0, 95);
            leds[i][2] = random(0, 20);
        }
        setBaseLedsIndividual(leds);
    }
}

// --- Mode 1: VISOR (Terminator / Predator Thermal-Crimson Live Camera HUD) ---
static void drawVisorFrame(uint32_t now) {
    cameraStepPreview();
    const uint8_t* luma = cameraGetLumaPreview();
    const uint8_t motion = cameraGetMotionScore();
    face.fillSprite(TFT_BLACK);
    for (int py = 0; py < 98; py++) {
        const uint8_t* row = luma + py * 160;
        for (int px = 0; px < 160; px++) {
            uint8_t v = row[px];
            uint16_t col;
            if (v < 64) col = face.color565(v * 2, 0, v / 2);
            else if (v < 140) col = face.color565(128 + (v - 64), (v - 64) * 2, 0);
            else if (v < 210) col = face.color565(255, 150 + (v - 140), 0);
            else col = face.color565(255, 255, (v - 210) * 5);
            face.fillRect(px * 2, py * 2, 2, 2, col);
        }
    }
    // Scanning laser line & HUD reticle
    int scanY = (now / 12) % 196;
    face.drawFastHLine(0, scanY, 320, TFT_GREEN);
    face.drawCircle(160, 98, 36, TFT_RED);
    face.drawCircle(160, 98, 6, TFT_YELLOW);
    face.drawFastHLine(110, 98, 36, TFT_RED);
    face.drawFastHLine(174, 98, 36, TFT_RED);
    face.drawFastVLine(160, 48, 36, TFT_RED);
    face.drawFastVLine(160, 112, 36, TFT_RED);
    face.setTextColor(TFT_YELLOW, TFT_BLACK);
    face.setCursor(6, 6);
    face.printf("VISOR TERMICO // MOV:%u%% LUX:%.0f PRX:%d", (unsigned)motion, s_lux, s_prox);
    face.setCursor(6, 182);
    face.print("TOCA LA IMAGEN PARA CRITICAR / DISPARAR IR");
    face.pushSprite(0, 0);
}

// --- Mode 2: RADAR (Autonomous Sentry Turret + Motion Alarm) ---
static uint32_t s_lastAlarmMs = 0;
static void drawRadarFrame(uint32_t now) {
    cameraStepPreview();
    const uint8_t motion = cameraGetMotionScore();
    const float t = now * 0.001f;
    // Sweep head automatically left-right
    float sweepDeg = sinf(t * 1.1f) * 60.0f;
    servoSetTarget(sweepDeg * (float)M_PI / 180.0f, 12.0f * (float)M_PI / 180.0f);

    const bool alarm = (motion >= 22);
    face.fillSprite(TFT_BLACK);
    const int cx = 160, cy = 178;
    const uint16_t gridCol = alarm ? TFT_RED : TFT_DARKGREEN;
    for (int r = 35; r <= 155; r += 30) {
        face.drawArc(cx, cy, r, r + 1, 180, 360, gridCol);
    }
    face.drawLine(cx, cy, cx - 145, cy - 80, gridCol);
    face.drawLine(cx, cy, cx, cy - 160, gridCol);
    face.drawLine(cx, cy, cx + 145, cy - 80, gridCol);

    // Sweeping radar needle matching head pan angle
    float radAngle = (-90.0f - sweepDeg) * (float)M_PI / 180.0f;
    int nx = cx + (int)(cosf(radAngle) * 152.0f);
    int ny = cy + (int)(sinf(radAngle) * 152.0f);
    face.drawWideLine(cx, cy, nx, ny, 3, alarm ? TFT_YELLOW : TFT_GREEN);

    if (motion >= 10) {
        int dist = constrain(150 - (int)motion, 35, 145);
        int bx = cx + (int)(cosf(radAngle) * dist);
        int by = cy + (int)(sinf(radAngle) * dist);
        face.fillCircle(bx, by, alarm ? 10 : 6, alarm ? TFT_RED : TFT_ORANGE);
        face.drawCircle(bx, by, 14, TFT_YELLOW);
    }

    face.setTextColor(alarm ? TFT_RED : TFT_GREEN, TFT_BLACK);
    face.setCursor(8, 6);
    face.printf("MODO CENTINELA // PAN:%+.0f  MOV:%u%%", sweepDeg, (unsigned)motion);
    if (alarm) {
        face.fillRect(50, 42, 220, 26, TFT_RED);
        face.setTextColor(TFT_WHITE, TFT_RED);
        face.setCursor(68, 51);
        face.print("!! INTRUSO DETECTADO !!");
        if (now - s_lastAlarmMs > 9000) {
            s_lastAlarmMs = now;
            audioBeep(1600, 90);
            s_wantIrZap = true;
            sendRobotEvent("intruder");
        }
    }
    face.pushSprite(0, 0);
}

// --- Mode 3: SYNTH (XY Touch Pad: Finger Controls Head + Demonic Theremin + Fire Trail) ---
struct TrailPt { int16_t x, y; uint8_t life; };
static TrailPt s_trail[20];
static int s_trailIdx = 0;
static float s_synthPan = 0.0f, s_synthTilt = 12.0f;
static int s_synthHz = 0;

static void drawSynthFrame(uint32_t now, bool touched, int tx, int ty) {
    face.fillSprite(TFT_BLACK);
    // Neon demonic grid
    for (int x = 0; x < 320; x += 32) face.drawFastVLine(x, 0, 196, TFT_MAROON);
    for (int y = 0; y < 196; y += 28) face.drawFastHLine(0, y, 320, TFT_MAROON);

    if (touched && ty < 194) {
        s_trail[s_trailIdx] = { (int16_t)tx, (int16_t)ty, 255 };
        s_trailIdx = (s_trailIdx + 1) % 20;
        // Map X (0..320) -> Pan (+85..-85 deg), Y (0..194) -> Tilt (+60..-5 deg)
        s_synthPan  = (160.0f - tx) * (85.0f / 160.0f);
        s_synthTilt = 60.0f - (ty * (65.0f / 194.0f));
        servoSetTarget(s_synthPan * (float)M_PI / 180.0f, s_synthTilt * (float)M_PI / 180.0f);
        s_synthHz = 140 + tx * 4 + (194 - ty) * 3;
        static uint32_t lastToneMs = 0;
        if (now - lastToneMs >= 75) {
            lastToneMs = now;
            audioBeep(s_synthHz, 55);
            setBaseLeds((tx * 40) / 320, ((194 - ty) * 40) / 194, 25);
        }
    }

    for (int i = 0; i < 20; i++) {
        if (s_trail[i].life > 15) {
            uint8_t l = s_trail[i].life;
            uint16_t c = (l > 180) ? TFT_YELLOW : (l > 100) ? TFT_ORANGE : TFT_RED;
            face.fillCircle(s_trail[i].x, s_trail[i].y, 3 + (l / 32), c);
            s_trail[i].life -= 15;
        }
    }

    face.setTextColor(TFT_CYAN, TFT_BLACK);
    face.setCursor(8, 6);
    face.printf("SYNTH XY // PAN:%+.0f TILT:%+.0f  %d Hz", s_synthPan, s_synthTilt, s_synthHz);
    face.setTextColor(TFT_YELLOW, TFT_BLACK);
    face.setCursor(8, 180);
    face.print("ARRASTRA EL DEDO: MUEVE LA CABEZA Y TOCA NOTAS");
    face.pushSprite(0, 0);
}

// --- Mode 4: GAME (Touch Arcade Game "Caza-Almas" / Soul Crusher) ---
struct SoulTarget {
    float x, y, vx, vy;
    uint16_t color;
};
static SoulTarget s_souls[4];
static bool s_soulsInit = false;
static int s_gameScore = 0;

static void initSouls() {
    const uint16_t cols[4] = { TFT_RED, TFT_ORANGE, TFT_MAGENTA, TFT_CYAN };
    for (int i = 0; i < 4; i++) {
        s_souls[i].x = 40 + i * 70;
        s_souls[i].y = 50 + (i % 2) * 60;
        s_souls[i].vx = (i & 1 ? 2.8f : -2.8f);
        s_souls[i].vy = (i < 2 ? 2.2f : -2.2f);
        s_souls[i].color = cols[i];
    }
    s_soulsInit = true;
}

static void drawGameFrame(uint32_t now, bool justPressed, int tx, int ty) {
    if (!s_soulsInit) initSouls();
    face.fillSprite(TFT_BLACK);
    // Mini flames at bottom
    for (int i = 0; i < 16; i++) {
        int cx = i * 20 + 10;
        int h = 14 + (int)(sinf(now * 0.008f + i) * 6.0f);
        face.fillTriangle(cx - 10, 196, cx + 10, 196, cx, 196 - h, TFT_MAROON);
        face.fillTriangle(cx - 6, 196, cx + 6, 196, cx, 196 - h + 5, TFT_RED);
    }

    for (int i = 0; i < 4; i++) {
        SoulTarget& s = s_souls[i];
        s.x += s.vx;
        s.y += s.vy;
        if (s.x < 24 || s.x > 296) s.vx = -s.vx;
        if (s.y < 32 || s.y > 168) s.vy = -s.vy;

        if (justPressed && ty < 194) {
            float dx = tx - s.x, dy = ty - s.y;
            if (dx * dx + dy * dy <= 32.0f * 32.0f) {
                s_gameScore++;
                audioBeep(1900, 45);
                setBaseLeds(40, 30, 0);
                face.fillCircle((int)s.x, (int)s.y, 28, TFT_YELLOW);
                // Nod head on hit
                servoSetTarget(random(-25, 26) * (float)M_PI / 180.0f, 22.0f * (float)M_PI / 180.0f);
                s.x = random(30, 290);
                s.y = random(36, 162);
                s.vx = (random(0, 2) ? 1 : -1) * (2.6f + min(3.0f, s_gameScore * 0.1f));
                s.vy = (random(0, 2) ? 1 : -1) * (2.2f + min(2.5f, s_gameScore * 0.1f));
                if (s_gameScore % 5 == 0) {
                    char buf[24];
                    snprintf(buf, sizeof(buf), "score:%d", s_gameScore);
                    sendRobotEvent(buf);
                }
            }
        }

        // Draw flaming skull/soul target
        int sx = (int)s.x, sy = (int)s.y;
        face.fillCircle(sx, sy, 18, s.color);
        face.fillCircle(sx, sy - 2, 13, TFT_WHITE);
        face.fillRect(sx - 8, sy + 6, 16, 8, TFT_WHITE);
        // Skull eyes & teeth
        face.fillCircle(sx - 5, sy - 3, 3, TFT_BLACK);
        face.fillCircle(sx + 5, sy - 3, 3, TFT_BLACK);
        face.drawFastVLine(sx - 4, sy + 8, 5, TFT_BLACK);
        face.drawFastVLine(sx,     sy + 8, 5, TFT_BLACK);
        face.drawFastVLine(sx + 4, sy + 8, 5, TFT_BLACK);
    }

    face.setTextColor(TFT_YELLOW, TFT_BLACK);
    face.setCursor(8, 6);
    face.printf("CAZA-ALMAS (TOCA LAS CALAVERAS) // ALMAS: %d", s_gameScore);
    face.pushSprite(0, 0);
}

// --- Mode 5: ORACLE (Cursed Tarot & Underworld Fortune Teller) ---
static const char* const TAROT_TITLES[8] = {
    "XIII - LA MUERTE",
    "XV - EL DIABLO",
    "XVI - LA TORRE EN LLAMAS",
    "XII - EL COLGADO",
    "X - RUEDA DEL CAOS",
    "XVIII - LUNA DE SANGRE",
    "0 - EL LOCO MORTAL",
    "XX - JUICIO ABISAL"
};
static const char* const TAROT_OMENS[8] = {
    "TU WIFI MORIRA HOY",
    "VENDERAS TU ALMA POR CAFE",
    "CAOS EN TU CODIGO FUENTE",
    "SUSPENSO EN EL INFRAMUNDO",
    "FORTUNA OSCURA INMINENTE",
    "NO MIRES DETRAS DE TI",
    "TU PEINADO ES UN PECADO",
    "EL ABISMO TE OBSERVA"
};
static int s_tarotCard = -1;
static uint32_t s_tarotDealMs = 0;

static void dealTarotCard(int cardIdx) {
    s_tarotCard = (cardIdx >= 0 && cardIdx < 8) ? cardIdx : random(0, 8);
    s_tarotDealMs = millis();
    audioBeep(520, 60);
    servoSetTarget(random(-20, 21) * (float)M_PI / 180.0f, 25.0f * (float)M_PI / 180.0f);
    char ev[48];
    snprintf(ev, sizeof(ev), "tarot:%s", TAROT_TITLES[s_tarotCard]);
    sendRobotEvent(ev);
}

static void drawOracleFrame(uint32_t now, bool justPressed, int tx, int ty) {
    (void)tx;
    if (justPressed && ty < 194) {
        dealTarotCard(-1);
    }
    face.fillSprite(TFT_BLACK);
    const float t = now * 0.001f;

    // Mystical starry background & rotating pentagram
    for (int i = 0; i < 18; i++) {
        int sx = (i * 73 + (now / 40)) % 320;
        int sy = (i * 41) % 190;
        face.fillCircle(sx, sy, (i % 3 == 0) ? 2 : 1, (i & 1) ? TFT_PURPLE : TFT_GOLD);
    }
    drawPentagram(160, 98, 78, t * 0.8f, TFT_PURPLE);

    if (s_tarotCard < 0) {
        // Crystal ball waiting to be touched
        face.fillCircle(160, 98, 46, face.color565(45, 0, 80));
        face.drawCircle(160, 98, 48, TFT_CYAN);
        face.drawCircle(160, 98, 38, TFT_MAGENTA);
        // All-seeing eye inside the crystal ball
        face.fillEllipse(160, 98, 26, 14, TFT_YELLOW);
        face.fillEllipse(160, 98, 8, 13, TFT_RED);
        face.fillCircle(160, 98, 4, TFT_BLACK);
        face.setTextColor(TFT_GOLD, TFT_BLACK);
        face.setCursor(46, 8);
        face.print("ORACULO DEL INFRAMUNDO // TAROT MALDITO");
        face.setTextColor(TFT_CYAN, TFT_BLACK);
        face.setCursor(32, 178);
        face.print("TOCA LA BOLA DE CRISTAL PARA LEER TU FUTURO");
    } else {
        // Draw ornate Tarot card in center
        face.fillRoundRect(46, 18, 228, 160, 8, face.color565(30, 5, 45));
        face.drawRoundRect(46, 18, 228, 160, 8, TFT_GOLD);
        face.drawRoundRect(50, 22, 220, 152, 6, TFT_ORANGE);

        // Card title
        face.setTextColor(TFT_GOLD);
        int tw = strlen(TAROT_TITLES[s_tarotCard]) * 6;
        face.setCursor(160 - tw / 2, 30);
        face.print(TAROT_TITLES[s_tarotCard]);

        // Central mystic emblem inside card
        int cx = 160, cy = 94;
        drawPentagram(cx, cy, 34, -t * 1.4f, TFT_RED);
        face.fillCircle(cx, cy, 12, TFT_YELLOW);
        face.fillCircle(cx - 4, cy - 2, 3, TFT_BLACK);
        face.fillCircle(cx + 4, cy - 2, 3, TFT_BLACK);

        // Omen subtitle
        face.setTextColor(TFT_CYAN);
        int ow = strlen(TAROT_OMENS[s_tarotCard]) * 6;
        face.setCursor(160 - ow / 2, 142);
        face.print(TAROT_OMENS[s_tarotCard]);

        face.setTextColor(TFT_LIGHTGREY);
        face.setCursor(74, 160);
        face.print("(TOCA OTRA VEZ PARA OTRA CARTA)");
    }
    face.pushSprite(0, 0);
}

// --- Mode 6: PET (Tamagotchi Demoníaco / Virtual Dark Lord) ---
static int s_petSouls = 65;
static int s_petRage  = 80;
static int s_petChaos = 70;
static uint32_t s_lastPetDecayMs = 0;

static void drawPetFrame(uint32_t now, bool justPressed, int tx, int ty) {
    if (now - s_lastPetDecayMs > 6000) {
        s_lastPetDecayMs = now;
        if (s_petSouls > 5) s_petSouls--;
        if (s_petChaos > 5) s_petChaos--;
    }

    if (justPressed && ty >= 148 && ty < 194) {
        if (tx < 106) {
            // [+ALMA] Feed soul
            s_petSouls = min(100, s_petSouls + 25);
            audioBeep(1600, 60);
            servoSetTarget(0.0f, 28.0f * (float)M_PI / 180.0f);
            sendRobotEvent("pet:feed");
        } else if (tx < 212) {
            // [RAYO] Electric torture / chaos boost
            s_petChaos = min(100, s_petChaos + 30);
            s_petRage  = min(100, s_petRage + 20);
            s_wantIrZap = true;
            audioBeep(550, 90);
            servoSetTarget(random(-35, 36) * (float)M_PI / 180.0f, 15.0f * (float)M_PI / 180.0f);
            sendRobotEvent("pet:zap");
        } else {
            // [MIMOS] Pet the demon
            s_petRage = max(10, s_petRage - 15);
            s_petChaos = min(100, s_petChaos + 15);
            audioBeep(1200, 70);
            servoSetTarget(0.0f, 5.0f * (float)M_PI / 180.0f);
            sendRobotEvent("pet:love");
        }
    }

    face.fillSprite(TFT_BLACK);
    face.setTextColor(TFT_GOLD, TFT_BLACK);
    face.setCursor(8, 6);
    face.print("TAMAGOTCHI DEL AVERNO // CUIDA A TU DEMONIO");

    // Draw animated bouncing mini-demon on the left (x = 70, y = 84)
    int bob = (int)(sinf(now * 0.008f) * 5.0f);
    int dx = 68, dy = 82 + bob;
    // Horns
    face.fillTriangle(dx - 28, dy - 16, dx - 14, dy - 26, dx - 34, dy - 42, TFT_GOLD);
    face.fillTriangle(dx + 28, dy - 16, dx + 14, dy - 26, dx + 34, dy - 42, TFT_GOLD);
    // Body/head
    face.fillCircle(dx, dy, 30, TFT_RED);
    face.drawCircle(dx, dy, 31, TFT_ORANGE);
    // Eyes
    face.fillCircle(dx - 11, dy - 5, 6, TFT_YELLOW);
    face.fillCircle(dx + 11, dy - 5, 6, TFT_YELLOW);
    face.fillCircle(dx - 11, dy - 5, 2, TFT_BLACK);
    face.fillCircle(dx + 11, dy - 5, 2, TFT_BLACK);
    // Fanged mouth
    face.fillRect(dx - 12, dy + 8, 24, 7, TFT_BLACK);
    face.fillTriangle(dx - 9, dy + 8, dx - 5, dy + 8, dx - 7, dy + 14, TFT_WHITE);
    face.fillTriangle(dx + 5, dy + 8, dx + 9, dy + 8, dx + 7, dy + 14, TFT_WHITE);

    // 3 Status Bars on the right (x = 135..308)
    const char* statNames[3] = { "ALMAS", "IRA", "CAOS" };
    const int   statVals[3]  = { s_petSouls, s_petRage, s_petChaos };
    const uint16_t statCols[3] = { TFT_CYAN, TFT_RED, TFT_MAGENTA };
    for (int i = 0; i < 3; i++) {
        int by = 34 + i * 34;
        face.setTextColor(TFT_WHITE);
        face.setCursor(132, by);
        face.printf("%s: %d%%", statNames[i], statVals[i]);
        face.drawRect(132, by + 11, 174, 12, TFT_LIGHTGREY);
        face.fillRect(134, by + 13, (170 * statVals[i]) / 100, 8, statCols[i]);
    }

    // 3 Action Touch Buttons at y = 152..188
    const char* acts[3] = { "[+ALMA]", "[RAYO]", "[MIMOS]" };
    const uint16_t acols[3] = { TFT_DARKCYAN, TFT_MAROON, TFT_PURPLE };
    for (int i = 0; i < 3; i++) {
        int bx = 8 + i * 104;
        face.fillRoundRect(bx, 152, 96, 34, 5, acols[i]);
        face.drawRoundRect(bx, 152, 96, 34, 5, TFT_YELLOW);
        face.setTextColor(TFT_WHITE);
        int tw = strlen(acts[i]) * 6;
        face.setCursor(bx + 48 - tw / 2, 165);
        face.print(acts[i]);
    }
    face.pushSprite(0, 0);
}

// --- Mode 7: SCANNER (Biometric Sin Scanner & Lie Detector) ---
static const char* const SIN_VERDICTS[6] = {
    "MENTIROSO PATOLOGICO",
    "PECADOR NIVEL JEFE",
    "PEREZA EXTREMA (99%)",
    "ALMA CORRUPTA",
    "CULPABLE DE CAOS",
    "HUMANO SOSPECHOSO"
};
static int s_sinVerdict = 0;
static int s_sinScore = 88;

static void triggerSinScan() {
    s_sinVerdict = random(0, 6);
    s_sinScore = random(65, 100);
    s_wantIrZap = true;
    audioBeep(1750, 80);
    char ev[48];
    snprintf(ev, sizeof(ev), "scan:%s (%d%%)", SIN_VERDICTS[s_sinVerdict], s_sinScore);
    sendRobotEvent(ev);
}

static void drawScannerFrame(uint32_t now, bool justPressed, int tx, int ty) {
    (void)tx;
    if (justPressed && ty < 194) {
        triggerSinScan();
    }
    cameraStepPreview();
    const uint8_t* luma = cameraGetLumaPreview();
    const uint8_t motion = cameraGetMotionScore();
    face.fillSprite(TFT_BLACK);

    // Matrix-green & crimson biometric silhouette from camera
    for (int py = 0; py < 98; py += 2) {
        const uint8_t* row = luma + py * 160;
        for (int px = 0; px < 160; px += 2) {
            uint8_t v = row[px];
            if (v > 55) {
                uint16_t col = (v > 165) ? TFT_GREEN : face.color565(0, v, 0);
                face.fillRect(px * 2, py * 2, 3, 3, col);
            }
        }
    }

    // Sweeping red/cyan biometric laser bar
    int scanY = (now / 8) % 196;
    face.fillRect(0, scanY, 320, 3, TFT_RED);
    face.drawFastHLine(0, (scanY + 2) % 196, 320, TFT_YELLOW);

    // Biometric targeting frame
    face.drawRect(50, 22, 220, 120, TFT_GREEN);
    // Polygraph / lie-detector waveform at y = 152
    int prevY = 152;
    for (int x = 10; x < 310; x += 6) {
        int amp = 4 + (motion / 4) + (audioMicOn() ? random(0, 14) : 2);
        int wy = 152 + (int)(sinf(now * 0.02f + x * 0.25f) * amp);
        face.drawLine(x - 6, prevY, x, wy, TFT_CYAN);
        prevY = wy;
    }

    face.fillRect(0, 0, 320, 18, TFT_BLACK);
    face.setTextColor(TFT_GREEN, TFT_BLACK);
    face.setCursor(6, 5);
    face.printf("ESCANER DE PECADOS // CORRUPCION: %d%%", s_sinScore);

    face.fillRect(0, 172, 320, 24, TFT_MAROON);
    face.setTextColor(TFT_YELLOW, TFT_MAROON);
    face.setCursor(8, 180);
    face.printf("VEREDICTO: %s (TOCA=JUZGAR)", SIN_VERDICTS[s_sinVerdict]);
    face.pushSprite(0, 0);
}

// "face <mood>", "mode <op>", "tarot <idx>", "scan_now", "rave", or "ir_zap" from the relay.
static bool onCommand(const char* cmd) {
    if (!strncmp(cmd, "face ", 5)) {
        for (int i = 0; i < MOOD_COUNT; i++) {
            if (!strcmp(cmd + 5, MOOD_NAMES[i])) {
                s_wantMood = i;
            }
        }
        return true;
    }
    if (!strncmp(cmd, "mode ", 5)) {
        for (int i = 0; i < OP_COUNT; i++) {
            if (!strcmp(cmd + 5, OP_NAMES[i])) s_opMode = i;
        }
        return true;
    }
    if (!strncmp(cmd, "tarot", 5)) {
        s_opMode = OP_ORACLE;
        int idx = (strlen(cmd) > 6) ? atoi(cmd + 6) : -1;
        dealTarotCard(idx);
        return true;
    }
    if (!strcmp(cmd, "scan_now")) {
        s_opMode = OP_SCANNER;
        triggerSinScan();
        return true;
    }
    if (!strcmp(cmd, "rave")) {
        s_raveUntilMs = millis() + 8000;
        s_wantIrZap = true;
        return true;
    }
    if (!strcmp(cmd, "ir_zap")) {
        s_wantIrZap = true;
        return true;
    }
    return audioCommand(cmd);
}

static void drawStatus() {
    LinkStats st;
    linkGetStats(&st);
    status.fillSprite(TFT_BLACK);
    status.setTextColor(TFT_LIGHTGREY);
    status.setCursor(4, 2);
    const char* state = audioMuted() ? "MUTE"
                      : audioSpeaking() ? "HABLANDO"
                      : audioMicOn() ? "ESCUCHANDO" : "...";
    status.printf("Bat %d%% [%s] %s", (int)M5.Power.getBatteryLevel(), state,
                  st.lastText[0] ? st.lastText : (st.relayOnline ? "USB-C OK" : "Sin relay"));

    // 4 Interactive Touch Buttons along y = 15..43 (80px wide each)
    const char* b0 = OP_LABELS[s_opMode];
    const char* b1 = "PODER";
    const char* b2 = "INSULTO";
    const char* b3 = audioMuted() ? "MIC:OFF" : "MIC:ON";
    const uint16_t c0 = TFT_PURPLE;
    const uint16_t c1 = TFT_MAROON;
    const uint16_t c2 = TFT_RED;
    const uint16_t c3 = audioMuted() ? TFT_DARKGREY : TFT_DARKGREEN;
    const char* labels[4] = { b0, b1, b2, b3 };
    const uint16_t cols[4] = { c0, c1, c2, c3 };

    for (int i = 0; i < 4; i++) {
        int bx = i * 80;
        status.fillRoundRect(bx + 2, 15, 76, 27, 4, cols[i]);
        status.drawRoundRect(bx + 2, 15, 76, 27, 4, TFT_ORANGE);
        status.setTextColor(TFT_WHITE);
        int tw = strlen(labels[i]) * 6;
        status.setCursor(bx + 40 - tw / 2, 25);
        status.print(labels[i]);
    }
    status.pushSprite(0, 240 - 44);
}

void setup() {
    // Servo current peaks cause brief voltage dips; don't reset on them.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    auto cfg = M5.config();
    cfg.output_power = true;
    M5.begin(cfg);
    Serial.setRxBufferSize(8192);
    Serial.begin(115200);
    M5.Display.setBrightness(95);   // vivid glow for the hellfire & demonic face
    M5.Display.fillScreen(TFT_BLACK);
    face.setColorDepth(16);
    face.setPsram(true);
    face.createSprite(320, 196);
    status.setColorDepth(16);
    status.createSprite(320, 44);
    status.setTextSize(1);

    M5.Speaker.begin();
    M5.Speaker.setVolume(120);

    bool base = boardInit();
    servoInit();
    setBaseLeds(40, 0, 12);
    bool camera = cameraInit();
    ltrInit();
    (void)base; (void)camera;

    WiFi.mode(WIFI_STA);
    WiFi.setHostname("stackchan");
    WiFi.setSleep(false);              // modem sleep adds 100+ ms of jitter to every packet
    WiFi.setAutoReconnect(true);
    s_wifiAttemptMs = millis();
    WiFi.begin(s_ssids[s_ssidIdx], WIFI_PASS);

    servoStartTask();
    M5.Speaker.tone(2000, 80);   // before audioStart(): nothing else uses the speaker yet
    delay(100);
    linkStart();
    audioStart();
    linkSetCommandHandler(onCommand);   // after audioStart(): ours first, then the audio ones
    s_linkStarted = true;
}

void loop() {
    i2cLock();   // touch, PMIC, LEDs: the audio task also uses this bus
    M5.update();
    static uint32_t lastUi = 0, lastBatt = 0, lastAnim = 0;
    static wl_status_t lastWifi = WL_IDLE_STATUS;
    uint32_t now = millis();

    pollHardwareSensors(now);

    wl_status_t w = WiFi.status();
    if (w != WL_CONNECTED && s_numSsids > 1 && now - s_wifiAttemptMs >= 15000) {
        s_wifiAttemptMs = now;
        s_ssidIdx = (s_ssidIdx + 1) % s_numSsids;
        WiFi.disconnect();
        WiFi.begin(s_ssids[s_ssidIdx], WIFI_PASS);
    }
    if (w != lastWifi) {
        lastWifi = w;
        if (w == WL_CONNECTED) {
            setBaseLeds(40, 0, 12);
        }
    }

    if (now - lastBatt >= 2000) {
        lastBatt = now;
        float busV, mA;
        if (!readBatteryCurrent(&busV, &mA)) mA = NAN;
        linkSetBattery(M5.Power.getBatteryVoltage() / 1000.0f, M5.Power.getBatteryLevel(), mA);
    }

    PendingCommands pc;
    linkTakePending(&pc);
    if (pc.brightness >= 0) M5.Display.setBrightness(constrain(pc.brightness, 0, 255));
    if (pc.wifiSleep >= 0) WiFi.setSleep(pc.wifiSleep == 1);
    if (pc.leds[0] >= 0) setBaseLeds(pc.leds[0], pc.leds[1], pc.leds[2]);

    // Handle touch input
    bool touched = M5.Touch.getCount() > 0;
    bool justPressed = false;
    int tx = 0, ty = 0;
    if (touched) {
        auto det = M5.Touch.getDetail();
        justPressed = det.wasPressed();
        tx = det.x;
        ty = det.y;
    }

    if (s_linkStarted && justPressed) {
        if (ty >= 196) {
            // Bottom 4-button touch bar
            int btn = constrain(tx / 80, 0, 3);
            if (btn == 0) {
                s_opMode = (s_opMode + 1) % OP_COUNT;
                audioBeep(1100 + s_opMode * 150, 45);
                char ev[24];
                snprintf(ev, sizeof(ev), "mode:%s", OP_NAMES[s_opMode]);
                sendRobotEvent(ev);
                drawStatus();
            } else if (btn == 1) {
                // PODER button: cycle special demonic power + IR blast
                static int pwrIdx = 0;
                const int powers[4] = { INFERNO, HYPNOSIS, GLITCH, LASER };
                s_opMode = OP_FACE;
                s_wantMood = powers[pwrIdx++ & 3];
                s_wantIrZap = true;
                audioBeep(600, 90);
                sendRobotEvent("power");
                drawStatus();
            } else if (btn == 2) {
                // INSULTO button: trigger immediate camera roast!
                audioBeep(1500, 60);
                s_wantMood = LASER;
                sendRobotEvent("roast");
                drawStatus();
            } else if (btn == 3) {
                // MIC button: mute/unmute
                audioTap();
                drawStatus();
            }
        } else if (s_opMode == OP_VISOR) {
            // Tapping the camera visor fires IR and triggers a roast
            s_wantIrZap = true;
            audioBeep(1700, 50);
            sendRobotEvent("roast");
        } else if (s_opMode == OP_FACE) {
            // Tapping the demon face cycles its expression and pokes it!
            s_wantMood = (s_wantMood + 1) % MOOD_COUNT;
            audioBeep(900, 40);
        }
    }

    if (now - lastUi >= 400) {
        lastUi = now;
        drawStatus();
    }

    if (s_wantIrZap) {
        s_wantIrZap = false;
        fireIrBeam();
    }

    // 25 FPS active mode animation + individual 12-LED effects
    if (now - lastAnim >= 40) {
        lastAnim = now;
        animateBaseLeds(now);
        if (s_opMode == OP_FACE) {
            s_mood = s_wantMood;
            int mouth = audioSpeaking() ? 2 : (audioMicOn() ? 1 : 0);
            drawHellfireFrame(s_mood, mouth, now);
        } else if (s_opMode == OP_VISOR) {
            drawVisorFrame(now);
        } else if (s_opMode == OP_RADAR) {
            drawRadarFrame(now);
        } else if (s_opMode == OP_SYNTH) {
            drawSynthFrame(now, touched, tx, ty);
        } else if (s_opMode == OP_GAME) {
            drawGameFrame(now, justPressed, tx, ty);
        } else if (s_opMode == OP_ORACLE) {
            drawOracleFrame(now, justPressed, tx, ty);
        } else if (s_opMode == OP_PET) {
            drawPetFrame(now, justPressed, tx, ty);
        } else if (s_opMode == OP_SCANNER) {
            drawScannerFrame(now, justPressed, tx, ty);
        }
    }

    i2cUnlock();
    delay(8);
}
