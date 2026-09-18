#pragma once

#include <Arduino.h>

void networkManagerBegin();
void networkManagerLoop();
void networkManagerStartBle();
void networkManagerStopBle();
void networkManagerStopWifi();
bool networkManagerConnectForSync();
bool networkManagerIsBleActive();
bool networkManagerTakeConfigUpdate();
bool networkManagerTakeWifiRetryRequest();
void networkManagerRetrySavedWifi();
String networkManagerDeviceName();

bool networkManagerApplyBlePayload(const String& payload);
String networkManagerLastConfigError();
String networkManagerProvisioningStatus();
bool networkManagerGetLocation(float& latitude, float& longitude);
void networkManagerRecordLog(const String& message);
void networkManagerShowWifiAttempt(const String& ssid, uint8_t attempt, size_t networkIndex, size_t networkCount);
