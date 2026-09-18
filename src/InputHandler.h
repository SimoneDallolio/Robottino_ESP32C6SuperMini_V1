#pragma once

// Interfaccia per inizializzare il pulsante e rilevare le pressioni valide.
#include <Arduino.h>

void initInput();
bool isButtonPressed();
bool isButtonHeldFor(unsigned long duration);