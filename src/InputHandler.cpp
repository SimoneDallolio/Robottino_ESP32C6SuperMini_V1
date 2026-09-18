#include "InputHandler.h"
#include "Config.h"

// Configura il pulsante con la resistenza interna: a riposo il pin e HIGH,
// mentre una pressione verso massa lo porta a LOW.
void initInput() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

bool isButtonPressed() {
  // Il debounce evita che una singola pressione venga interpretata piu volte
  // a causa dei rapidi cambiamenti elettrici del contatto meccanico.
  static bool lastState = HIGH;
  static unsigned long lastDebounceTime = 0;
  bool reading = digitalRead(BUTTON_PIN);
  bool clicked = false;

  if (reading != lastState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > 50) {
    static bool buttonState = HIGH;
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        // La funzione segnala l'evento solo sul fronte di pressione, non mentre
        // il pulsante resta mantenuto.
        clicked = true;
      }
    }
  }
  lastState = reading;
  return clicked;
}

bool isButtonHeldFor(unsigned long duration) {
  static bool wasPressed = false;
  static bool eventSent = false;
  static unsigned long pressedAt = 0;
  bool pressed = digitalRead(BUTTON_PIN) == LOW;

  if (!pressed) {
    wasPressed = false;
    eventSent = false;
    return false;
  }

  if (!wasPressed) {
    wasPressed = true;
    pressedAt = millis();
  }

  if (!eventSent && millis() - pressedAt >= duration) {
    eventSent = true;
    return true;
  }
  return false;
}