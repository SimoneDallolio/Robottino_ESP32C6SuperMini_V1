#pragma once

// Funzioni per sincronizzare l'ora e disegnare la schermata dell'orologio.
#include <Adafruit_SSD1306.h>

bool initTimeNTP();
void clockModuleLoop();
void renderClockScreen(Adafruit_SSD1306& display);