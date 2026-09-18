#pragma once

// Interfaccia del modulo grafico che disegna i diversi stati del volto OLED.
#include <Adafruit_SSD1306.h>
#include "Config.h"

void resetFaceAwakeAnimation();
void renderFaceAwake(Adafruit_SSD1306& display, int offsetX = 0);
void renderFaceShake(Adafruit_SSD1306& display, unsigned long now);
void renderFaceFallingAsleep(Adafruit_SSD1306& display, unsigned long startTime, FaceState& currentState);
void renderFaceSleeping(Adafruit_SSD1306& display);
void renderFaceWakingUp(Adafruit_SSD1306& display, unsigned long startTime, FaceState& currentState);
