#include "ClockModule.h"
#include "Config.h"
#include "NetworkManager.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "time.h"

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
    // Data abbreviata nell'angolo in alto a sinistra.
    char dateText[8];
    snprintf(dateText, sizeof(dateText), "%s %02d", months[timeinfo.tm_mon], timeinfo.tm_mday);
    display.setTextSize(1);
    display.setCursor(2, 2);
    display.print(dateText);

    // L'orario resta centrato nel display.
    char timeText[6];
    snprintf(timeText, sizeof(timeText), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    display.setTextSize(3);
    int16_t textX;
    int16_t textY;
    uint16_t textWidth;
    uint16_t textHeight;
    display.getTextBounds(timeText, 0, 0, &textX, &textY, &textWidth, &textHeight);
    // Compensa l'origine interna del font per centrare il rettangolo reale
    // del testo, non soltanto il punto di inserimento del cursore.
    display.setCursor((SCREEN_WIDTH - textWidth) / 2 - textX,
              (SCREEN_HEIGHT - textHeight) / 2 - textY);
    display.print(timeText);

    // Mini termometro in basso a destra. La temperatura viene ottenuta da
    // Open-Meteo usando le coordinate salvate in NVS.
    display.setTextSize(1);
    char temperatureValue[8];
    if (!isnan(currentTemperature)) {
      snprintf(temperatureValue, sizeof(temperatureValue), "%.1f", currentTemperature);
    } else {
      snprintf(temperatureValue, sizeof(temperatureValue), "--");
    }
    display.getTextBounds(temperatureValue, 0, 0, &textX, &textY, &textWidth, &textHeight);

    // Il font standard del display non contiene sempre il carattere "°".
    // Lo disegniamo quindi come un piccolo cerchio tra il valore e la C.
    int temperatureY = SCREEN_HEIGHT - textHeight - 1;
    int temperatureX = SCREEN_WIDTH - textWidth - 12;
    display.setCursor(temperatureX, temperatureY);
    display.print(temperatureValue);
    display.drawCircle(temperatureX + textWidth + 3, temperatureY + 2, 1, SSD1306_WHITE);
    display.setCursor(temperatureX + textWidth + 6, temperatureY);
    display.print("C");
  } else {
    display.setTextSize(1);
    display.setCursor(29, 28);
    display.print("Ora non sync");
  }
}
