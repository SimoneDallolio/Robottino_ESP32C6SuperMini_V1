#include "BLEConfigModule.h"
#include "NetworkManager.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include "Config.h"

namespace {
  NimBLEServer* server = nullptr;
  NimBLECharacteristic* configCharacteristic = nullptr;
  NimBLECharacteristic* statusCharacteristic = nullptr;
  bool active = false;

  class ConfigCallbacks final : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
      String payload = characteristic->getValue().c_str();
      if (payload.isEmpty()) {
        bleConfigNotify("{\"ok\":false,\"error\":\"payload vuoto\"}");
        return;
      }

      bool accepted = networkManagerApplyBlePayload(payload);
      JsonDocument request;
      deserializeJson(request, payload);
      String action = request["action"] | "";
      // Le richieste diagnostiche inviano gia una risposta JSON dettagliata
      // dal gestore di rete; non sovrascriverla con la conferma generica.
      if (action == "scanWifi" || action == "savedNetworks" || action == "logs" || action == "retryWifi" ||
          action == "saveWifi" || action == "deleteWifi" || action == "stopBle") return;
      bleConfigNotify(accepted
        ? "{\"ok\":true,\"message\":\"configurazione salvata\"}"
        : String("{\"ok\":false,\"error\":\"") + networkManagerLastConfigError() + "\"}" );
    }
  };

  ConfigCallbacks callbacks;

  class StatusCallbacks final : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
      String status = networkManagerProvisioningStatus();
      characteristic->setValue(status.c_str());
    }
  };

  StatusCallbacks statusCallbacks;
}

void bleConfigBegin(const String& deviceName) {
  if (active) return;

  NimBLEDevice::init(deviceName.c_str());
  server = NimBLEDevice::createServer();
  // NimBLE 2 non riavvia automaticamente l'advertising dopo una disconnessione.
  server->advertiseOnDisconnect(true);
  NimBLEService* service = server->createService(BLE_SERVICE_UUID);
  configCharacteristic = service->createCharacteristic(
    BLE_CONFIG_CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY
  );
  configCharacteristic->setCallbacks(&callbacks);
  statusCharacteristic = service->createCharacteristic(
    BLE_STATUS_CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ
  );
  statusCharacteristic->setCallbacks(&statusCallbacks);
  statusCharacteristic->setValue(networkManagerProvisioningStatus().c_str());

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  // L'app PWA individua il dispositivo tramite il prefisso del nome.
  advertising->setName(deviceName.c_str());
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->enableScanResponse(true);
  advertising->start();
  active = true;
  networkManagerRecordLog(String("[BLE] Disponibile per la configurazione: ") + deviceName);
}

void bleConfigStop() {
  if (!active) return;
  NimBLEDevice::getAdvertising()->stop();
  // Non rilasciare la memoria del controller: il BLE deve poter essere
  // riavviato con la pressione prolungata senza riavviare l'ESP32.
  NimBLEDevice::deinit(false);
  server = nullptr;
  configCharacteristic = nullptr;
  statusCharacteristic = nullptr;
  active = false;
  networkManagerRecordLog("[BLE] Configurazione BLE disattivata");
}

void bleConfigNotify(const String& message) {
  if (!active || configCharacteristic == nullptr) return;
  configCharacteristic->setValue(message.c_str());
  configCharacteristic->notify();
}
