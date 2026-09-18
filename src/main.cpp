#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_system.h>

#include "Config.h"
#include "InputHandler.h"
#include "FaceModule.h"
#include "ClockModule.h"
#include "NetworkManager.h"
#include "AccelerometerModule.h"

// Modulo principale: inizializza l'hardware, sceglie la schermata da mostrare
// e gestisce le interazioni con il pulsante.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Stato iniziale del robot: volto sveglio.
ScreenMode currentScreen = MODE_FACE;
FaceState faceState = FACE_AWAKE;

// Timestamp usati per calcolare inattivita e durata dell'addormentamento.
unsigned long lastActivityTime = 0;
unsigned long sleepAnimationStart = 0;
unsigned long wakeAnimationStart = 0;
unsigned long shakeAnimationStart = 0;
constexpr unsigned long SHAKE_ANIMATION_DURATION_MS = 650;

enum StartupState {
  STARTUP_CONNECTING,
  STARTUP_LOADING,
  STARTUP_CONFIGURE,
  STARTUP_READY
};

StartupState startupState = STARTUP_CONNECTING;
// La schermata iniziale resta visibile finché l'utente non interagisce.
// Una pressione breve permette comunque di usare volto e orologio mentre
// il BLE rimane disponibile per la configurazione.
bool showStartupScreen = true;
constexpr unsigned long DISPLAY_FRAME_INTERVAL_MS = 50; // 20 fps: fluido senza saturare I2C.
unsigned long lastDisplayFrameAt = 0;

// Mostrata direttamente dal gestore Wi-Fi mentre l'avvio è bloccato in attesa
// della connessione: rende visibile rete e giro di tentativo sull'OLED.
void networkManagerShowWifiAttempt(const String& ssid, uint8_t attempt, size_t networkIndex, size_t networkCount) {
  String visibleSsid = ssid;
  if (visibleSsid.length() > 18) visibleSsid = visibleSsid.substring(0, 17) + ".";

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 5);
  display.print("Connessione Wi-Fi");
  display.setCursor(0, 25);
  display.print(visibleSsid);
  display.setCursor(0, 45);
  display.print("Rete ");
  display.print(networkIndex);
  display.print("/");
  display.print(networkCount);
  display.print("  tent. ");
  display.print(attempt);
  display.print("/3");
  display.display();
}

void renderStartupScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (startupState == STARTUP_CONNECTING) {
    display.setCursor(18, 24);
    display.print("Connessione WiFi...");
  } else if (startupState == STARTUP_LOADING) {
    display.setCursor(25, 20);
    display.print("Avvio in corso");
    display.setCursor(35, 38);
    display.print("Attendere...");
  } else if (startupState == STARTUP_CONFIGURE) {
    display.setCursor(8, 12);
    display.print(networkManagerDeviceName());
    display.setCursor(7, 29);
    display.print("Configura il robot");
    display.setCursor(13, 43);
    display.print("dall'app BLE");
  }

  display.display();
}

void updateDisplay() {
  // Ogni fotogramma viene preparato in memoria e poi trasferito all'OLED in
  // un'unica operazione, evitando residui del fotogramma precedente.
  display.clearDisplay();

  if (currentScreen == MODE_FACE) {
    if (shakeAnimationStart != 0 && millis() - shakeAnimationStart < SHAKE_ANIMATION_DURATION_MS) {
      renderFaceShake(display, millis());
    } else if (faceState == FACE_AWAKE) {
      renderFaceAwake(display, 0);
    } else if (faceState == FACE_FALLING_ASLEEP) {
      renderFaceFallingAsleep(display, sleepAnimationStart, faceState);
    } else if (faceState == FACE_SLEEPING) {
      renderFaceSleeping(display);
    } else if (faceState == FACE_WAKING_UP) {
      renderFaceWakingUp(display, wakeAnimationStart, faceState);
    }
  } 
  else if (currentScreen == MODE_CLOCK) {
    renderClockScreen(display);
  }

  // Mostra il fotogramma appena composto sul display fisico.
  display.display();
}

void renderCurrentScreenIfDue() {
  unsigned long now = millis();
  if (now - lastDisplayFrameAt < DISPLAY_FRAME_INTERVAL_MS) return;
  lastDisplayFrameAt = now;
  if (startupState == STARTUP_READY || !showStartupScreen) {
    updateDisplay();
  } else {
    renderStartupScreen();
  }
}

void setup() {
  networkManagerRecordLog("[BOOT] Robottino avviato");

  // -------------------------------------------------------------------
  // 2. Hardware I/O e Bus I2C
  // -------------------------------------------------------------------
  initInput();
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  Wire.setClock(400000L);

  // -------------------------------------------------------------------
  // 3. Display OLED SSD1306
  // -------------------------------------------------------------------
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    networkManagerRecordLog("[ERROR] SSD1306 non trovato sul bus I2C!");
    // Riavvio automatico di emergenza dopo 5 secondi invece di un blocco infinito
    delay(5000);
    esp_restart();
  }
  accelerometerBegin();

  // Schermata iniziale di avvio
  startupState = STARTUP_LOADING;
  renderStartupScreen();

  // -------------------------------------------------------------------
  // 4. Gestione Rete, BLE e NTP
  // -------------------------------------------------------------------
  networkManagerBegin();

  if (networkManagerIsBleActive()) {
    startupState = STARTUP_CONFIGURE;
  } else {
    // Tenta la sincronizzazione NTP
    if (initTimeNTP()) {
      startupState = STARTUP_READY;
    } else {
      startupState = STARTUP_CONFIGURE;
    }
  }

  // Aggiorna lo schermo con lo stato finale senza bloccare l'avvio del loop.
  renderStartupScreen();

  // -------------------------------------------------------------------
  // 5. Inizializzazione Stato Globale e Seed Casuale
  // -------------------------------------------------------------------
  randomSeed(esp_random());
  resetFaceAwakeAnimation();
  lastActivityTime = millis();

  networkManagerRecordLog("[BOOT] Setup completato");
}

void loop() {
  networkManagerLoop();
  if (networkManagerTakeWifiRetryRequest()) networkManagerRetrySavedWifi();
  accelerometerLoop();
  if (accelerometerTakeShakeEvent()) {
    shakeAnimationStart = millis();
    currentScreen = MODE_FACE;
    // Una scossa sveglia completamente il robot, anche dallo stato di sonno,
    // e fa ripartire il conto alla rovescia prima dell'addormentamento.
    faceState = FACE_AWAKE;
    resetFaceAwakeAnimation();
    lastActivityTime = shakeAnimationStart;
    showStartupScreen = false;
    networkManagerRecordLog("[ACC] Robot risvegliato dalla scossa");
  }
  if (isButtonHeldFor(3000)) {
    networkManagerStartBle();
    showStartupScreen = true;
    networkManagerRecordLog("[INPUT] Pressione prolungata: BLE riattivato");
    lastActivityTime = millis();
  }

  if (networkManagerTakeConfigUpdate()) {
    networkManagerStopBle();
    startupState = STARTUP_LOADING;
    showStartupScreen = true;
    renderStartupScreen();
    if (initTimeNTP()) {
      startupState = STARTUP_READY;
    } else {
      startupState = STARTUP_CONFIGURE;
    }
  }

  // Una pressione sveglia il robot oppure alterna tra volto e orologio.
  if (isButtonPressed()) {
    lastActivityTime = millis();
    showStartupScreen = false;
    networkManagerRecordLog("[INPUT] Pulsante esterno premuto (GPIO 2)");

    if (faceState == FACE_SLEEPING || faceState == FACE_FALLING_ASLEEP) {
      faceState = FACE_WAKING_UP;
      wakeAnimationStart = millis();
      resetFaceAwakeAnimation();
      currentScreen = MODE_FACE;
    } else if (currentScreen == MODE_FACE) {
      currentScreen = MODE_CLOCK;
    } else if (currentScreen == MODE_CLOCK) {
      currentScreen = MODE_FACE;
    }
  }

  // Dopo il timeout, l'orologio torna al volto; se il volto era gia sveglio,
  // parte invece l'animazione che lo porta allo stato di sonno.
  if (millis() - lastActivityTime > INACTIVITY_TIMEOUT) {
    if (currentScreen == MODE_CLOCK) {
      currentScreen = MODE_FACE;
    } else if (currentScreen == MODE_FACE && faceState == FACE_AWAKE) {
      faceState = FACE_FALLING_ASLEEP;
      sleepAnimationStart = millis();
    }
  }

  // Mantiene reattivi pulsante, BLE e animazioni.
  clockModuleLoop();
  renderCurrentScreenIfDue();
  delay(5);
}
