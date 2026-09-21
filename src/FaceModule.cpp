#include "FaceModule.h"

// Questo modulo contiene tutto il rendering del volto sul display OLED:
// occhi e animazioni di addormentamento e sonno (senza bocca).

// Dimensioni e coordinate degli occhi — centrati verticalmente e orizzontalmente.
// Display: 128x64. Due occhi da 34px, gap 8px, margine 26px per lato: (128-76)/2=26.
// Centro verticale: eyeY = (64 - eyeHeight) / 2 = 17
// Radius moderato per angoli smussati ma forma chiaramente rettangolare.
const int eyeWidth = 34;
const int eyeHeight = 30;
const int eyeRadius = 7;
const int leftEyeX = 26;           // (128 - 34 - 8 - 34) / 2 = 26
const int rightEyeX = 68;          // 26 + 34 + 8
const int eyeY = 17;               // (64 - 30) / 2

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
    lookOffsetX = random(2) == 0 ? -6 : 6;
    gestureEndsAt = now + randomBetween(700, 1700);
  } else {
    awakeGesture = AWAKE_BLINKING;
    gestureEndsAt = now + randomBetween(120, 190);
  }
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

  // Gli occhi si chiudono verso il basso: il bordo inferiore resta fisso a eyeY+eyeHeight.
  int currentH = eyeHeight - (int)(progress * (eyeHeight - 4));
  int currentY = eyeY + eyeHeight - currentH;

  display.fillRoundRect(leftEyeX, currentY, eyeWidth, currentH, eyeRadius, SSD1306_WHITE);
  display.fillRoundRect(rightEyeX, currentY, eyeWidth, currentH, eyeRadius, SSD1306_WHITE);
}

void renderFaceSleeping(Adafruit_SSD1306& display) {
  // Un'oscillazione verticale molto piccola simula la respirazione durante il sonno.
  int breathOffset = (int)(sin(millis() / 600.0) * 2.5);

  // Occhi chiusi allineati al bordo INFERIORE degli occhi aperti:
  // eyeY + eyeHeight - 4 = 17 + 30 - 4 = 43.
  int closedEyeY = eyeY + eyeHeight - 4 + breathOffset;
  display.fillRoundRect(leftEyeX, closedEyeY, eyeWidth, 4, 2, SSD1306_WHITE);
  display.fillRoundRect(rightEyeX, closedEyeY, eyeWidth, 4, 2, SSD1306_WHITE);

  int zStage = (millis() / 700) % 4;
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Le Z partono dall'alto e salgono verso destra (posizioni originali).
  if (zStage >= 1) { display.setCursor(112, 36 + breathOffset); display.print("z"); }
  if (zStage >= 2) { display.setCursor(118, 26 + breathOffset); display.print("Z"); }
  if (zStage >= 3) { display.setCursor(121, 16 + breathOffset); display.print("Z"); }
}

void renderFaceWakingUp(Adafruit_SSD1306& display, unsigned long startTime, FaceState& currentState) {
  // Parte dagli occhi quasi chiusi e aumenta progressivamente l'apertura
  // con due blink parziali, poi apertura completa.
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
    int maximumEyeHeight = 10 + (blinkNumber * 8);

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
    // Dopo il secondo blink gli occhi si aprono completamente senza scatto.
    currentH = sleepingEyeHeight +
               (((elapsed - partialBlinkDuration) * (eyeHeight - sleepingEyeHeight)) / 400);
  }

  // Il bordo inferiore resta fisso a eyeY+eyeHeight: gli occhi si aprono verso l'alto.
  int currentY = eyeY + eyeHeight - currentH;
  display.fillRoundRect(leftEyeX, currentY, eyeWidth, currentH, eyeRadius, SSD1306_WHITE);
  display.fillRoundRect(rightEyeX, currentY, eyeWidth, currentH, eyeRadius, SSD1306_WHITE);
}

void renderFaceShake(Adafruit_SSD1306& display, unsigned long now) {
  // Oscillazione sinusoidale veloce: ±16 px sull'asse X.
  int offsetX = (int)(sin(now * 0.08) * 16);

  // Disegna entrambi gli occhi con la stessa logica.
  const int exArr[2] = { leftEyeX + offsetX, rightEyeX + offsetX };

  for (int i = 0; i < 2; i++) {
    int ex = exArr[i];

    // --- Passo 1: rettangolo smussato bianco (arrotonda tutti e 4 gli angoli) ---
    display.fillRoundRect(ex, eyeY, eyeWidth, eyeHeight, eyeRadius, SSD1306_WHITE);

    // --- Passo 2: squadra gli angoli INFERIORI ridisegnando i corner con bianco ---
    // fillRoundRect lascia nero il quadrante di raggio eyeRadius in ogni angolo;
    // riempiendolo di bianco si ottengono angoli retti solo in basso.
    display.fillRect(ex,                       eyeY + eyeHeight - eyeRadius, eyeRadius, eyeRadius, SSD1306_WHITE);
    display.fillRect(ex + eyeWidth - eyeRadius, eyeY + eyeHeight - eyeRadius, eyeRadius, eyeRadius, SSD1306_WHITE);

    // --- Passo 3: incavo ad arco al centro del bordo inferiore ---
    // notchW = 30 lascia (34-30)/2 = 2 px di "piede" per ciascun lato.
    // notchR = notchH = 6: le pareti sono un quarto di cerchio esatto —
    // partono verticali dal bordo inferiore e arrivano orizzontali al top piatto.
    const int notchW = 30;  // larghezza dell'incavo
    const int notchH = 6;   // profondita' visibile (6px)
    const int notchR = 6;   // raggio = profondita': curva fluida da verticale a orizzontale
    int notchX = ex + (eyeWidth - notchW) / 2;
    int notchY = eyeY + eyeHeight - notchH;
    // Il rect nero fuoriesce di notchR sotto il bordo (area gia' nera): visibile solo l'arco.
    display.fillRoundRect(notchX, notchY, notchW, notchH + notchR, notchR, SSD1306_BLACK);
  }
}
