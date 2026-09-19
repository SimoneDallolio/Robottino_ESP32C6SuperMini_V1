#include "ClockModule.h"
#include "Config.h"
#include "NetworkManager.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "time.h"
#include "soc/usb_serial_jtag_struct.h"

// Server e regola del fuso orario usati dalla sincronizzazione NTP.
const char* ntpServer = "pool.ntp.org";
// Configurazione fuso orario Italia (CET/CEST) con gestione automatica ora legale
const char* timeZone = "CET-1CEST,M3.5.0,M10.5.0/3"; 

const char* months[] = {"GEN", "FEB", "MAR", "APR", "MAG", "GIU",
                        "LUG", "AGO", "SET", "OTT", "NOV", "DIC"};

float currentTemperature = NAN;
constexpr unsigned long ECO_SYNC_INTERVAL = 30UL * 60UL * 1000UL;
constexpr uint8_t MAX_WEATHER_ATTEMPTS = 3;
constexpr unsigned long WEATHER_RETRY_DELAY_MS = 1000;
unsigned long lastEcoSync = 0;

static bool getWeatherForCoordinates(float latitude, float longitude) {
  String weatherUrl = "https://api.open-meteo.com/v1/forecast?latitude=" +
                      String(latitude, 4) + "&longitude=" + String(longitude, 4) +
                      "&current=temperature_2m&timezone=auto";
  for (uint8_t attempt = 1; attempt <= MAX_WEATHER_ATTEMPTS; attempt++) {
    networkManagerRecordLog(String("[METEO] Richiesta ") + String(attempt) + "/3");
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient weatherRequest;
  weatherRequest.setConnectTimeout(3500);
  weatherRequest.setTimeout(3500);
    weatherRequest.useHTTP10(true);

    if (weatherRequest.begin(client, weatherUrl)) {
      int weatherStatus = weatherRequest.GET();
      networkManagerRecordLog(String("[METEO] Tentativo ") + String(attempt) + "/3, HTTP status: " + String(weatherStatus));
      if (weatherStatus == HTTP_CODE_OK) {
        JsonDocument weatherDocument;
        // Legge il JSON direttamente dallo stream HTTP, evitando una seconda
        // copia completa della risposta nella RAM dell'ESP32.
        DeserializationError weatherError = deserializeJson(weatherDocument, weatherRequest.getStream());
        weatherRequest.end();
        if (!weatherError) {
          currentTemperature = weatherDocument["current"]["temperature_2m"].as<float>();
          if (!isnan(currentTemperature)) {
            networkManagerRecordLog(String("[METEO] Temperatura: ") + String(currentTemperature, 1) + " C");
            return true;
          }
          networkManagerRecordLog("[METEO] Temperatura non presente nella risposta");
        } else {
          networkManagerRecordLog(String("[METEO] Errore JSON: ") + weatherError.c_str());
        }
      } else {
        if (weatherStatus < 0) {
          networkManagerRecordLog(String("[METEO] Connessione o DNS falliti: ") + String(weatherStatus));
        } else {
          networkManagerRecordLog(String("[METEO] HTTP non valido: ") + String(weatherStatus));
        }
        weatherRequest.end();
      }
    } else {
      networkManagerRecordLog("[METEO] Errore apertura richiesta Open-Meteo");
    }

    if (attempt < MAX_WEATHER_ATTEMPTS) delay(WEATHER_RETRY_DELAY_MS);
  }
  return false;
}

static bool syncTimeAndWeather() {
  if (!networkManagerConnectForSync()) {
    networkManagerRecordLog("[NTP] Nessuna rete disponibile per l'aggiornamento");
    lastEcoSync = millis();
    return false;
  }

  networkManagerRecordLog(String("[NTP] Wi-Fi connesso, IP: ") + WiFi.localIP().toString());
    // Imposta il fuso orario italiano e avvia la sincronizzazione con il server.
    configTzTime(timeZone, ntpServer);
    
    // Attende la prima sincronizzazione dell'ora
    struct tm timeinfo;
    int syncAttempts = 0;
    while (!getLocalTime(&timeinfo) && syncAttempts < 6) {
      delay(500);
      syncAttempts++;
    }

    if (syncAttempts < 6) {
      networkManagerRecordLog("[NTP] Ora sincronizzata");
    } else {
      networkManagerRecordLog("[NTP] Sincronizzazione ora fallita");
    }

    float latitude;
    float longitude;
    networkManagerGetLocation(latitude, longitude);
    networkManagerRecordLog(String("[METEO] Coordinate: ") + String(latitude, 4) + ", " + String(longitude, 4));
    bool weatherUpdated = getWeatherForCoordinates(latitude, longitude);
    if (!weatherUpdated) {
      networkManagerRecordLog("[METEO] Recupero dati fallito: verra mostrato -- C");
      // Riattiva il BLE: l'app può così chiedere il registro diagnostico e
      // l'utente può intervenire sulla configurazione della rete.
      networkManagerRecordLog("[BLE] Sincronizzazione fallita: BLE riattivato per diagnosi");
      networkManagerStartBle();
    }

  networkManagerStopWifi();
  lastEcoSync = millis();
  return syncAttempts < 6 && weatherUpdated;
}

bool initTimeNTP() {
  return syncTimeAndWeather();
}

void clockModuleLoop() {
  if (!networkManagerIsBleActive() && millis() - lastEcoSync >= ECO_SYNC_INTERVAL) {
    syncTimeAndWeather();
  }
}

void renderClockScreen(Adafruit_SSD1306& display) {
  // Recupera l'ora locale e la disegna solo se la sincronizzazione e riuscita.
  struct tm timeinfo;
  bool hasTime = getLocalTime(&timeinfo);

  if (hasTime) {
    // 1. Temperatura in alto a sinistra.
    display.setTextSize(1);
    char temperatureValue[8];
    if (!isnan(currentTemperature)) {
      snprintf(temperatureValue, sizeof(temperatureValue), "%.1f", currentTemperature);
    } else {
      snprintf(temperatureValue, sizeof(temperatureValue), "--");
    }
    
    int16_t tempX, tempY;
    uint16_t tempWidth, tempHeight;
    display.getTextBounds(temperatureValue, 0, 0, &tempX, &tempY, &tempWidth, &tempHeight);
    
    display.setCursor(2, 2);
    display.print(temperatureValue);
    display.drawCircle(2 + tempWidth + 3, 4, 1, SSD1306_WHITE);
    display.setCursor(2 + tempWidth + 6, 2);
    display.print("C");

    // 2. Indicatore Batteria in alto a destra
    // La percentuale viene aggiornata ogni 30 secondi con la media di 16 campioni
    // per eliminare il rumore dell'ADC ed evitare il flickering tra due valori.
    static int batteryPercentage = 0;
    static unsigned long lastBattUpdate = 0;
    const unsigned long BATT_UPDATE_INTERVAL = 30000UL; // 30 secondi
    if (millis() - lastBattUpdate >= BATT_UPDATE_INTERVAL || lastBattUpdate == 0) {
      long adcSum = 0;
      const int NUM_SAMPLES = 16;
      for (int i = 0; i < NUM_SAMPLES; i++) {
        adcSum += analogRead(BATTERY_ADC_PIN);
        delay(2);
      }
      float vGpio = ((adcSum / (float)NUM_SAMPLES) / 4095.0f) * 3.3f;
      float vBatt = vGpio * 2.0f;
      batteryPercentage = (int)(((vBatt - 3.0f) / (4.2f - 3.0f)) * 100.0f);
      if (batteryPercentage < 0)   batteryPercentage = 0;
      if (batteryPercentage > 100) batteryPercentage = 100;
      lastBattUpdate = millis();
    }
    char battText[8];
    snprintf(battText, sizeof(battText), "%d%%", batteryPercentage);
    int16_t battX, battY;
    uint16_t battWidth, battHeight;
    display.getTextBounds(battText, 0, 0, &battX, &battY, &battWidth, &battHeight);
    
    int iconWidth = 14; 
    int totalBattWidth = battWidth + 2 + iconWidth;
    int battCursorX = SCREEN_WIDTH - totalBattWidth - 2;
    int battCursorY = 2;
    
    display.setCursor(battCursorX, battCursorY);
    display.print(battText);
    
    int iconX = battCursorX + battWidth + 2;
    int iconY = battCursorY + 1;

    // Rileva USB-C leggendo il contatore SOF hardware del controller USB.
    // Quando la USB è collegata, l'host invia un SOF packet ogni 1ms e il contatore cambia.
    // Quando è scollegata, il contatore rimane fermo.
    // Nessun pin aggiuntivo necessario: è tutto hardware interno all'ESP32-C6.
    static uint16_t prevSofCount = 0;
    static bool isCharging = false;
    uint16_t currSofCount = (uint16_t)(USB_SERIAL_JTAG.fram_num.sof_frame_index);
    isCharging = (currSofCount != prevSofCount);
    prevSofCount = currSofCount;

    if (isCharging) {
      // Icona fulmine: indica la ricarica in corso
      display.drawLine(iconX + 4, iconY,     iconX + 1, iconY + 3, SSD1306_WHITE); // diagonale alta
      display.drawLine(iconX + 1, iconY + 3, iconX + 3, iconY + 3, SSD1306_WHITE); // segmento orizzontale
      display.drawLine(iconX + 3, iconY + 3, iconX,     iconY + 6, SSD1306_WHITE); // diagonale bassa
    } else {
      // Icona della batteria
      display.drawRect(iconX, iconY, 12, 6, SSD1306_WHITE);
      display.fillRect(iconX + 12, iconY + 2, 2, 2, SSD1306_WHITE);      // Polo positivo
      int fillWidth = (batteryPercentage * 10) / 100;
      if (fillWidth > 0) {
        display.fillRect(iconX + 1, iconY + 1, fillWidth, 4, SSD1306_WHITE); // Livello interno
      }
    }

    // 3. L'orario perfettamente centrato nello schermo.
    char timeText[6];
    snprintf(timeText, sizeof(timeText), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    display.setTextSize(3);
    int16_t timeX, timeY;
    uint16_t timeWidth, timeHeight;
    display.getTextBounds(timeText, 0, 0, &timeX, &timeY, &timeWidth, &timeHeight);
    
    int timeCursorX = (SCREEN_WIDTH - timeWidth) / 2 - timeX;
    int timeCursorY = (SCREEN_HEIGHT - timeHeight) / 2 - timeY; 
    display.setCursor(timeCursorX, timeCursorY);
    display.print(timeText);

    // 4. Data spostata in basso al centro (stessa distanza dal bordo usata in alto).
    char dateText[8];
    snprintf(dateText, sizeof(dateText), "%s %02d", months[timeinfo.tm_mon], timeinfo.tm_mday);
    display.setTextSize(1);
    int16_t dateX, dateY;
    uint16_t dateWidth, dateHeight;
    display.getTextBounds(dateText, 0, 0, &dateX, &dateY, &dateWidth, &dateHeight);
    
    int dateCursorX = (SCREEN_WIDTH - dateWidth) / 2 - dateX;
    int dateCursorY = SCREEN_HEIGHT - dateHeight - 2; // 2 pixel di margine dal basso
    display.setCursor(dateCursorX, dateCursorY);
    display.print(dateText);

  } else {
    display.setTextSize(1);
    display.setCursor(29, 28);
    display.print("Ora non sync");
  }
}
