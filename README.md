# Robottino ESP32-C6 SuperMini

Questo progetto è il firmware per un piccolo robot interattivo basato sulla scheda **ESP32-C6 SuperMini**. 
Il robot dispone di un display OLED per mostrare un volto animato, l'orario e altre informazioni, e reagisce ai movimenti tramite un accelerometro hardware integrato.

## Funzionalità Principali

*   **Volto Animato ed Espressioni**: Il robot ha uno stato "sveglio" con animazioni e reagisce se lasciato inattivo (animazione di addormentamento e sonno).
*   **Orologio Digitale Sincronizzato**: Una modalità orologio alternativa che mostra:
    *   Ora e data sincronizzate automaticamente tramite server NTP via Wi-Fi.
    *   Temperatura locale in tempo reale (ottenuta tramite le API di Open-Meteo).
    *   Livello di carica della batteria con misurazione hardware reale.
    *   Riconoscimento istantaneo del collegamento USB-C (appare l'icona del fulmine durante la ricarica).
*   **Risveglio col Movimento**: Grazie all'accelerometro (GY-521 / MPU-6050), il robot percepisce quando viene mosso.
*   **Gestione Energetica Intelligente**: Entra in modalità sleep dopo 15 minuti di inattività. La carica della batteria viene monitorata stabilizzando i valori dell'ADC, per una lettura precisa.
*   **Connettività BLE e Wi-Fi**: Moduli per la comunicazione Bluetooth Low Energy e la connessione internet.

## Collegamenti Hardware (Pinout)

Il circuito richiede i seguenti collegamenti ai pin dell'ESP32-C6 SuperMini:

| Componente | Pin / Collegamento |
| :--- | :--- |
| **OLED SDA** & **Accelerometro SDA** | `GPIO 6` (Bus I2C condiviso) |
| **OLED SCL** & **Accelerometro SCL** | `GPIO 7` (Bus I2C condiviso) |
| **Pulsante** | `GPIO 2` (collegato verso GND) |
| **Controllo Batteria** | `GPIO 3` (tramite partitore 100kΩ/100kΩ tra B+ e B-) |

> **Nota per l'I2C**: Poiché l'ESP32-C6 ha un solo bus I2C hardware esposto, il display OLED e l'accelerometro (GY-521) condividono gli stessi pin (SDA 6, SCL 7). Assicurarsi che gli indirizzi I2C dei due moduli non siano in conflitto (tipicamente OLED è `0x3C` e MPU-6050 è `0x68`).

> **Nota per la batteria**: Il partitore di tensione 100k/100k riduce a metà la tensione della batteria LiPo (massimo 4.2V diviso 2 = 2.1V), rendendola sicura per la lettura tramite l'ADC a 3.3V del pin GPIO 3.

## Struttura del Codice

Il firmware è suddiviso in moduli per una gestione pulita dell'hardware:
*   `Config.h`: Parametri centralizzati, pinout e stati.
*   `ClockModule`: Gestisce il layout dell'orologio, il calcolo della carica batteria (filtrato per rimuovere il rumore ADC), e il rilevamento SOF hardware per l'USB.
*   `AccelerometerModule`: Gestisce le letture MPU-6050 sul bus condiviso.
*   `NetworkManager`: Gestisce la sincronizzazione oraria e le chiamate HTTP(S) per il meteo.

## Compilazione

Il progetto utilizza **PlatformIO**. Per compilarlo e caricarlo sulla scheda, è sufficiente usare il task *Build* e *Upload* dall'estensione PlatformIO (es. su VS Code), assicurandosi che l'ambiente di destinazione `esp32-c6-devkitc-1` sia selezionato.
