/*
 * @Description: Circular progress-ring animation driven by the Adafruit ANO
 *               Rotary Navigation Encoder w/ Scroll Wheel (seesaw, PID 5740).
 *               Scroll wheel = ring/pointer angle, SELECT = new random emoji,
 *               UP/DOWN/LEFT/RIGHT = pan the whole animation.
 * @License: GPL 3.0
 */

#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <Wire.h>
#include <esp_random.h>
#include "Adafruit_seesaw.h"

// ANO scroll wheel encoder (seesaw) I2C address + button/d-pad pins
#define SEESAW_ADDR 0x49
#define SS_SWITCH_SELECT 1
#define SS_SWITCH_UP     2
#define SS_SWITCH_LEFT   3
#define SS_SWITCH_DOWN   4
#define SS_SWITCH_RIGHT  5

Adafruit_seesaw ss;
bool encoderFound = false;
int32_t lastEncoderPosition = 0;

// Display setup. `panel` talks to the physical AMOLED; `gfx` is an off-screen
// RAM framebuffer (Arduino_Canvas) with the identical drawing API. Every
// frame is drawn into `gfx` invisibly, then gfx->flush() blits the whole
// buffer to `panel` in one shot -- that's what kills the flicker, since the
// panel never shows a partially-drawn frame.
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS /* CS */, LCD_SCLK /* SCK */, LCD_SDIO0 /* SDIO0 */, LCD_SDIO1 /* SDIO1 */,
    LCD_SDIO2 /* SDIO2 */, LCD_SDIO3 /* SDIO3 */);

Arduino_GFX *panel = new Arduino_CO5300(bus, LCD_RST /* RST */,
                                        0 /* rotation */, false /* IPS */, LCD_WIDTH, LCD_HEIGHT,
                                        20 /* col offset 1 */, 0 /* row offset 1 */, 0 /* col_offset2 */, 0 /* row_offset2 */);

Arduino_GFX *gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);

// Ring geometry
const int16_t RING_OUTER_R = 92;
const int16_t RING_TRACK_THICK = 14;
const int16_t RING_INNER_R = RING_OUTER_R - RING_TRACK_THICK;
const int16_t FACE_R = RING_INNER_R - 6;
const int16_t BASE_CX = LCD_WIDTH / 2;
const int16_t BASE_CY = LCD_HEIGHT / 2;
const int16_t OFFSET_STEP = 14;
const int16_t OFFSET_MIN_X = -(BASE_CX - RING_OUTER_R - 4);
const int16_t OFFSET_MAX_X = (LCD_WIDTH - BASE_CX - RING_OUTER_R - 4);
const int16_t OFFSET_MIN_Y = -(BASE_CY - RING_OUTER_R - 4);
const int16_t OFFSET_MAX_Y = (LCD_HEIGHT - BASE_CY - RING_OUTER_R - 4);

// Encoder counts for one full physical spin of the scroll wheel. 90 needed
// ~4-5 turns to fill the ring, so the wheel outputs roughly 90/4.5 =~ 20
// counts/rev; lower this if one turn still overshoots a full circle, raise
// it if one turn now undershoots.
const int32_t COUNTS_PER_REV = 20;

// Rotation / emoji state
int32_t rotationCount = 0;   // unbounded raw encoder count
uint8_t emojiType = 0;       // which face/icon is currently shown
int16_t offsetX = 0;
int16_t offsetY = 0;
bool needsRedraw = true;

enum { EMOJI_SMILEY, EMOJI_WINK, EMOJI_SUNGLASSES, EMOJI_SURPRISED, EMOJI_HEART, EMOJI_STAR, EMOJI_COUNT };

// Function to convert HSV to RGB565
uint16_t HSVtoRGB(uint16_t h, uint8_t s, uint8_t v) {
    float fH = h / 60.0;
    float fS = s / 255.0;
    float fV = v / 255.0;

    int i = (int)fH;
    float f = fH - i;
    float p = fV * (1.0 - fS);
    float q = fV * (1.0 - fS * f);
    float t = fV * (1.0 - fS * (1.0 - f));

    float r, g, b;
    switch(i) {
        case 0: r = fV; g = t; b = p; break;
        case 1: r = q; g = fV; b = p; break;
        case 2: r = p; g = fV; b = t; break;
        case 3: r = p; g = q; b = fV; break;
        case 4: r = t; g = p; b = fV; break;
        default: r = fV; g = p; b = q; break;
    }

    uint8_t rVal = (uint8_t)(r * 255);
    uint8_t gVal = (uint8_t)(g * 255);
    uint8_t bVal = (uint8_t)(b * 255);

    return ((rVal & 0xF8) << 8) | ((gVal & 0xFC) << 3) | (bVal >> 3);
}

// Current pointer/progress angle in degrees, 0-359 (0 = 3 o'clock, increases clockwise on screen)
float currentAngle() {
    int32_t counts = rotationCount % COUNTS_PER_REV;
    if (counts < 0) counts += COUNTS_PER_REV;
    return (counts * 360.0f) / COUNTS_PER_REV;
}

void pickRandomEmoji() {
    uint8_t next;
    do {
        next = esp_random() % EMOJI_COUNT;
    } while (next == emojiType && EMOJI_COUNT > 1);
    emojiType = next;
}

void drawEyeDot(int16_t x, int16_t y, int16_t r) {
    gfx->fillCircle(x, y, r, BLACK);
}

void drawSmileArc(int16_t cx, int16_t cy, int16_t r, float startDeg, float endDeg) {
    gfx->fillArc(cx, cy, (int16_t)(r * 0.55f), (int16_t)(r * 0.55f) - 5, startDeg, endDeg, BLACK);
}

void drawFace(int16_t cx, int16_t cy, int16_t r) {
    uint16_t yellow = HSVtoRGB(48, 255, 255);
    gfx->fillCircle(cx, cy, r, yellow);

    int16_t eyeOffX = r * 0.35f;
    int16_t eyeOffY = r * 0.22f;
    int16_t eyeR = r * 0.11f;

    switch (emojiType) {
        case EMOJI_SMILEY:
            drawEyeDot(cx - eyeOffX, cy - eyeOffY, eyeR);
            drawEyeDot(cx + eyeOffX, cy - eyeOffY, eyeR);
            drawSmileArc(cx, cy, r, 25, 155);
            break;

        case EMOJI_WINK:
            drawEyeDot(cx - eyeOffX, cy - eyeOffY, eyeR);
            gfx->fillRoundRect(cx + eyeOffX - eyeR, cy - eyeOffY - 2, eyeR * 2, 4, 2, BLACK);
            drawSmileArc(cx, cy, r, 15, 165);
            break;

        case EMOJI_SUNGLASSES:
            gfx->fillRoundRect(cx - r * 0.62f, cy - eyeOffY - r * 0.14f, r * 1.24f, r * 0.28f, 6, BLACK);
            drawSmileArc(cx, cy, r, 25, 155);
            break;

        case EMOJI_SURPRISED:
            drawEyeDot(cx - eyeOffX, cy - eyeOffY, eyeR * 1.3f);
            drawEyeDot(cx + eyeOffX, cy - eyeOffY, eyeR * 1.3f);
            gfx->fillCircle(cx, cy + r * 0.35f, r * 0.16f, BLACK);
            break;
    }
}

void drawHeart(int16_t cx, int16_t cy, int16_t r) {
    uint16_t red = HSVtoRGB(0, 255, 230);
    float lobeR = r * 0.36f;
    int16_t lobeOffX = r * 0.32f;
    int16_t lobeOffY = r * 0.22f;
    gfx->fillCircle(cx - lobeOffX, cy - lobeOffY, lobeR, red);
    gfx->fillCircle(cx + lobeOffX, cy - lobeOffY, lobeR, red);
    gfx->fillTriangle(cx - r * 0.62f, cy - lobeOffY * 0.4f,
                       cx + r * 0.62f, cy - lobeOffY * 0.4f,
                       cx, cy + r * 0.72f, red);
}

void drawStar(int16_t cx, int16_t cy, int16_t r) {
    uint16_t yellow = HSVtoRGB(50, 255, 255);
    const int points = 5;
    float outerR = r * 0.85f;
    float innerR = r * 0.38f;
    int16_t xs[points * 2];
    int16_t ys[points * 2];

    for (int i = 0; i < points * 2; i++) {
        float rad = (i * 36 - 90) * DEG_TO_RAD;
        float radius = (i % 2 == 0) ? outerR : innerR;
        xs[i] = cx + (int16_t)(cosf(rad) * radius);
        ys[i] = cy + (int16_t)(sinf(rad) * radius);
    }
    for (int i = 0; i < points * 2; i++) {
        int j = (i + 1) % (points * 2);
        gfx->fillTriangle(cx, cy, xs[i], ys[i], xs[j], ys[j], yellow);
    }
}

void drawEmoji(int16_t cx, int16_t cy, int16_t r) {
    switch (emojiType) {
        case EMOJI_HEART:
            drawHeart(cx, cy, r);
            break;
        case EMOJI_STAR:
            drawStar(cx, cy, r);
            break;
        default:
            drawFace(cx, cy, r);
            break;
    }
}

void drawFrame() {
    int16_t cx = BASE_CX + offsetX;
    int16_t cy = BASE_CY + offsetY;
    float angle = currentAngle();

    gfx->fillScreen(BLACK);

    // Background track (unfilled ring)
    gfx->fillArc(cx, cy, RING_OUTER_R, RING_INNER_R, 0, 360, (uint16_t)0x2104);

    // Progress fill from 0 up to the current rotation angle
    uint16_t ringColor = HSVtoRGB((uint16_t)angle, 255, 255);
    gfx->fillArc(cx, cy, RING_OUTER_R, RING_INNER_R, 0, angle, ringColor);

    // Pointer knob riding on the ring at the current angle
    float rad = angle * DEG_TO_RAD;
    int16_t midR = (RING_OUTER_R + RING_INNER_R) / 2;
    int16_t knobX = cx + (int16_t)(cosf(rad) * midR);
    int16_t knobY = cy + (int16_t)(sinf(rad) * midR);
    gfx->fillCircle(knobX, knobY, 7, WHITE);
    gfx->drawCircle(knobX, knobY, 7, BLACK);

    // Emoji inside the ring
    drawEmoji(cx, cy, FACE_R);

    if (!encoderFound) {
        gfx->setTextColor(RED, BLACK);
        gfx->setTextSize(1);
        gfx->setCursor(30, LCD_HEIGHT - 20);
        gfx->println("Encoder not found!");
    }

    gfx->flush();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\nRotary Encoder Progress Ring Starting...");

    // Display setup
    pinMode(LCD_EN, OUTPUT);
    digitalWrite(LCD_EN, HIGH);
    delay(100);

    if (!gfx->begin()) { // also runs panel->begin() and allocates the framebuffer
        Serial.println("Canvas framebuffer allocation failed (out of PSRAM?)");
    }
    gfx->fillScreen(BLACK);
    gfx->flush();

    // I2C setup (must happen before ss.begin so custom pins stick)
    Wire.begin(IIC_SDA, IIC_SCL, 400000);
    delay(100);

    gfx->setTextColor(WHITE, BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(30, 20);
    gfx->println("Rotary Control");
    gfx->setTextSize(1);

    Serial.print("Looking for seesaw at 0x");
    Serial.println(SEESAW_ADDR, HEX);

    encoderFound = ss.begin(SEESAW_ADDR);
    if (!encoderFound) {
        Serial.println("Couldn't find seesaw encoder!");
        gfx->setCursor(40, 70);
        gfx->println("Encoder not found!");
        gfx->setCursor(40, 90);
        gfx->println("Check wiring / address");
    } else {
        Serial.println("Seesaw encoder found!");
        ss.pinMode(SS_SWITCH_SELECT, INPUT_PULLUP);
        ss.pinMode(SS_SWITCH_UP, INPUT_PULLUP);
        ss.pinMode(SS_SWITCH_DOWN, INPUT_PULLUP);
        ss.pinMode(SS_SWITCH_LEFT, INPUT_PULLUP);
        ss.pinMode(SS_SWITCH_RIGHT, INPUT_PULLUP);
        lastEncoderPosition = ss.getEncoderPosition();

        gfx->setCursor(30, 60);
        gfx->println("Scroll: move pointer");
        gfx->setCursor(30, 80);
        gfx->println("SELECT: new emoji");
        gfx->setCursor(30, 100);
        gfx->println("Arrows: pan animation");
    }
    gfx->flush();

    randomSeed(esp_random());
    pickRandomEmoji();

    delay(2000);
    gfx->fillScreen(BLACK);
    gfx->flush();

    // Brightness fade in
    for (int i = 0; i <= 200; i++) {
        panel->Display_Brightness(i);
        delay(2);
    }
}

void loop() {
    unsigned long currentTime = millis();

    if (encoderFound) {
        // Scroll wheel -> pointer / progress angle
        int32_t newPosition = ss.getEncoderPosition();
        int32_t delta = newPosition - lastEncoderPosition;
        if (delta != 0) {
            lastEncoderPosition = newPosition;
            rotationCount += delta;
            needsRedraw = true;
        }

        // SELECT -> new random emoji
        static unsigned long selectDebounce = 0;
        if (!ss.digitalRead(SS_SWITCH_SELECT) && (currentTime - selectDebounce > 250)) {
            selectDebounce = currentTime;
            pickRandomEmoji();
            needsRedraw = true;
        }

        // D-pad -> pan the whole animation (auto-repeats while held)
        static unsigned long panDebounce = 0;
        if (currentTime - panDebounce > 120) {
            bool panned = false;
            if (!ss.digitalRead(SS_SWITCH_UP) && offsetY > OFFSET_MIN_Y) {
                offsetY -= OFFSET_STEP;
                panned = true;
            }
            if (!ss.digitalRead(SS_SWITCH_DOWN) && offsetY < OFFSET_MAX_Y) {
                offsetY += OFFSET_STEP;
                panned = true;
            }
            if (!ss.digitalRead(SS_SWITCH_LEFT) && offsetX > OFFSET_MIN_X) {
                offsetX -= OFFSET_STEP;
                panned = true;
            }
            if (!ss.digitalRead(SS_SWITCH_RIGHT) && offsetX < OFFSET_MAX_X) {
                offsetX += OFFSET_STEP;
                panned = true;
            }
            if (panned) {
                panDebounce = currentTime;
                needsRedraw = true;
            }
        }
    }

    if (needsRedraw) {
        needsRedraw = false;
        drawFrame();
    }

    delay(10);
}
