#pragma once

// Configurazione centrale dell'hardware, dei tempi e degli stati del robot.
// Questo file viene incluso dagli altri moduli per condividere i parametri
// della scheda e gli stati usati dal programma principale.

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define OLED_SDA_PIN 6
#define OLED_SCL_PIN 7

// Bus I2C condiviso per OLED e GY-521 / MPU-6050.
// Ricollega il GY-521 a questi pin.
#define ACCELEROMETER_SDA_PIN 6
#define ACCELEROMETER_SCL_PIN 7

#define BUTTON_PIN 2 // Pulsante collegato tra GPIO 2 e GND.

#define BLE_SERVICE_UUID "7f7b0001-8b3a-4f65-9d39-2c8c5a7e1001"
#define BLE_CONFIG_CHARACTERISTIC_UUID "7f7b0002-8b3a-4f65-9d39-2c8c5a7e1001"
#define BLE_STATUS_CHARACTERISTIC_UUID "7f7b0003-8b3a-4f65-9d39-2c8c5a7e1001"

const unsigned long INACTIVITY_TIMEOUT = 15UL * 60UL * 1000UL; // Quindici minuti prima del sonno.

enum ScreenMode {
  // Schermata con il volto animato.
  MODE_FACE,
  // Schermata con l'orario sincronizzato via Wi-Fi.
  MODE_CLOCK
};

enum FaceState {
  // Il robot mostra il volto nella sua animazione normale.
  FACE_AWAKE,
  // Il robot sta eseguendo l'animazione di addormentamento.
  FACE_FALLING_ASLEEP,
  // Il robot dorme e mostra occhi chiusi e le lettere "Z".
  FACE_SLEEPING,
  // Il robot si sta svegliando con alcuni battiti parziali delle palpebre.
  FACE_WAKING_UP
};
