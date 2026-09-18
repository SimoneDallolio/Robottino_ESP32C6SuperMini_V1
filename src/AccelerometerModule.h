#pragma once

#include <Arduino.h>

// GY-521 / MPU-6050 sul proprio bus I2C, separato dall'OLED.
void accelerometerBegin();
void accelerometerLoop();
bool accelerometerTakeShakeEvent();
