#include "FaceModule.h"

// Questo modulo contiene tutto il rendering del volto sul display OLED:
// occhi, bocca e animazioni di addormentamento e sonno.

// Dimensioni e coordinate degli occhi
const int eyeWidth = 24;
const int eyeHeight = 24;
const int eyeRadius = 3;
const int leftEyeX = 34;
const int rightEyeX = 70;
const int eyeY = 14;

enum AwakeGesture {
  AWAKE_RESTING,
  AWAKE_LOOKING,
  AWAKE_BLINKING
};

static AwakeGesture awakeGesture = AWAKE_RESTING;
static unsigned long gestureEndsAt = 0;
static int lookOffsetX = 0;

static unsigned long randomBetween(unsigned long minimum, unsigned long maximum) {
  return minimum + random(maximum - minimum + 1);
}

void resetFaceAwakeAnimation() {
  awakeGesture = AWAKE_RESTING;
  lookOffsetX = 0;
  gestureEndsAt = millis() + randomBetween(1200, 3500);
}

static void chooseNextAwakeGesture() {
  unsigned long now = millis();

  // Sguardi e battiti sono scelti senza una sequenza fissa.
  if (random(100) < 55) {
    awakeGesture = AWAKE_LOOKING;
    lookOffsetX = random(2) == 0 ? -8 : 8;
    gestureEndsAt = now + randomBetween(700, 1700);
  } else {
    awakeGesture = AWAKE_BLINKING;
    gestureEndsAt = now + randomBetween(120, 190);
  }
}

static void drawMouthAwake(Adafruit_SSD1306& display, int offsetX, int offsetY) {
  // La bocca segue solo in parte il movimento degli occhi per rendere il volto
  // piu naturale durante lo spostamento dello sguardo.
  int mouthCenterX = 64 + (offsetX / 2);
  int mouthBaseY = 48 + (offsetY / 2);

  display.drawLine(mouthCenterX - 8, mouthBaseY, mouthCenterX + 1, mouthBaseY, SSD1306_WHITE);
  display.drawLine(mouthCenterX + 1, mouthBaseY, mouthCenterX + 5, mouthBaseY - 1, SSD1306_WHITE);
  display.drawLine(mouthCenterX + 5, mouthBaseY - 1, mouthCenterX + 8, mouthBaseY - 3, SSD1306_WHITE);
  display.drawLine(mouthCenterX + 8, mouthBaseY - 3, mouthCenterX + 10, mouthBaseY - 6, SSD1306_WHITE);
}

void renderFaceAwake(Adafruit_SSD1306& display, int offsetX) {
  // Dopo pause di durata variabile, il robot sceglie casualmente uno sguardo
  // laterale oppure un battito di ciglia.
  unsigned long now = millis();
  if (gestureEndsAt == 0) {
    resetFaceAwakeAnimation();
  } else if ((long)(now - gestureEndsAt) >= 0) {
    if (awakeGesture == AWAKE_RESTING) {
      chooseNextAwakeGesture();
    } else {
      awakeGesture = AWAKE_RESTING;
      lookOffsetX = 0;
      gestureEndsAt = now + randomBetween(1500, 5000);
    }
  }

  int eyeMoveX = 0;
  int currentEyeH = eyeHeight;

  if (awakeGesture == AWAKE_LOOKING) {
    eyeMoveX = lookOffsetX;
  } else if (awakeGesture == AWAKE_BLINKING) {
    currentEyeH = 2;
  }

  int adjustedY = eyeY + (eyeHeight - currentEyeH) / 2;

  display.fillRoundRect(leftEyeX + eyeMoveX + offsetX, adjustedY, eyeWidth, currentEyeH, eyeRadius, SSD1306_WHITE);
  display.fillRoundRect(rightEyeX + eyeMoveX + offsetX, adjustedY, eyeWidth, currentEyeH, eyeRadius, SSD1306_WHITE);
  drawMouthAwake(display, eyeMoveX + offsetX, 0);
}

void renderFaceFallingAsleep(Adafruit_SSD1306& display, unsigned long startTime, FaceState& currentState) {
  // La transizione dura circa 1,2 secondi e riduce progressivamente l'altezza
  // degli occhi fino a trasformarli nelle linee dello stato di sonno.
  unsigned long elapsed = millis() - startTime;
  float progress = (float)elapsed / 1200.0;

  if (progress >= 1.0) {
    // Lo stato viene aggiornato qui, al termine dell'animazione, cosi il ciclo
    // principale puo iniziare a disegnare il volto addormentato.
    currentState = FACE_SLEEPING;
    return;
  }

  int currentH = 24 - (int)(progress * 20.0);
  int currentY = 14 + (int)(progress * 14.0);

  display.fillRoundRect(leftEyeX, currentY, eyeWidth, currentH, 2, SSD1306_WHITE);
  display.fillRoundRect(rightEyeX, currentY, eyeWidth, currentH, 2, SSD1306_WHITE);

  if (progress < 0.5) {
    drawMouthAwake(display, 0, 0);
  } else {
    display.drawLine(56, 48, 72, 48, SSD1306_WHITE);
  }
}

void renderFaceSleeping(Adafruit_SSD1306& display) {
  // Un'oscillazione verticale molto piccola simula la respirazione durante il sonno.
  int breathOffset = (int)(sin(millis() / 600.0) * 2.5);

  int closedEyeY = 28 + breathOffset;
  display.fillRoundRect(leftEyeX, closedEyeY, eyeWidth, 4, 2, SSD1306_WHITE);
  display.fillRoundRect(rightEyeX, closedEyeY, eyeWidth, 4, 2, SSD1306_WHITE);

  display.drawLine(56, 48 + breathOffset, 72, 48 + breathOffset, SSD1306_WHITE);

  int zStage = (millis() / 700) % 4;
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Le lettere vengono aggiunte una alla volta per creare un'animazione ciclica.
  if (zStage >= 1) { display.setCursor(94, 20 + breathOffset); display.print("z"); }
  if (zStage >= 2) { display.setCursor(103, 11 + breathOffset); display.print("Z"); }
  if (zStage >= 3) { display.setCursor(113, 2 + breathOffset); display.print("Z"); }
}

void renderFaceWakingUp(Adafruit_SSD1306& display, unsigned long startTime, FaceState& currentState) {
  // Parte dagli occhi quasi chiusi e aumenta progressivamente l'apertura:
  // 9 e 14 pixel nei due blink, poi apertura completa a 24 pixel.
  const unsigned long blinkDuration = 400;
  const unsigned long partialBlinkDuration = blinkDuration * 2;
  const unsigned long wakeDuration = partialBlinkDuration + 400;
  const int sleepingEyeHeight = 4;
  unsigned long elapsed = millis() - startTime;

  if (elapsed >= wakeDuration) {
    currentState = FACE_AWAKE;
    renderFaceAwake(display, 0);
    return;
  }

  int currentH;
  if (elapsed < partialBlinkDuration) {
    unsigned long blinkTime = elapsed % blinkDuration;
    int blinkNumber = elapsed / blinkDuration;
    int maximumEyeHeight = 9 + (blinkNumber * 5);

    if (blinkTime < 150) {
      currentH = sleepingEyeHeight +
                 ((blinkTime * (maximumEyeHeight - sleepingEyeHeight)) / 150);
    } else if (blinkTime < 300) {
      currentH = maximumEyeHeight -
                 (((blinkTime - 150) * (maximumEyeHeight - sleepingEyeHeight)) / 150);
    } else {
      currentH = sleepingEyeHeight;
    }
  } else {
    // Dopo il terzo blink gli occhi si aprono completamente senza scatto.
    currentH = sleepingEyeHeight +
               (((elapsed - partialBlinkDuration) * (eyeHeight - sleepingEyeHeight)) / 400);
  }

  int currentY = eyeY + (eyeHeight - currentH) / 2;
  display.fillRoundRect(leftEyeX, currentY, eyeWidth, currentH, eyeRadius, SSD1306_WHITE);
  display.fillRoundRect(rightEyeX, currentY, eyeWidth, currentH, eyeRadius, SSD1306_WHITE);

  // Durante il risveglio la bocca resta neutra e orizzontale.
  display.drawLine(56, 48, 72, 48, SSD1306_WHITE);
}

void renderFaceShake(Adafruit_SSD1306& display, unsigned long now) {
  display.fillRoundRect(leftEyeX, eyeY, eyeWidth, eyeHeight, eyeRadius, SSD1306_WHITE);
  display.fillRoundRect(rightEyeX, eyeY, eyeWidth, eyeHeight, eyeRadius, SSD1306_WHITE);
  display.drawFastHLine(54, 50, 20, SSD1306_WHITE);
  int shift = ((now / 45) % 2 == 0) ? -3 : 3;
  // Scie laterali tratteggiate: sull'OLED monocromatico simulano opacita' ridotta.
  for (int side : {-1, 1}) for (int row = 0; row < 3; row++) {
    int x = (side < 0 ? 3 : 108) + shift * side;
    int y = 16 + row * 11;
    display.drawFastHLine(x, y, 12, SSD1306_WHITE);
    display.drawFastHLine(x + 3, y + 3, 6, SSD1306_WHITE);
  }
}
