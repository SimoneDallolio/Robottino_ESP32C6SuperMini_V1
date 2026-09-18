#include "AccelerometerModule.h"
#include <Wire.h>
#include <math.h>
#include "Config.h"
#include "NetworkManager.h"

namespace {
constexpr uint8_t ADDRESSES[] = {0x68, 0x69};
constexpr uint8_t REG_WHO_AM_I = 0x75, REG_POWER = 0x6B, REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_ACCEL = 0x3B;
constexpr unsigned long SAMPLE_INTERVAL_MS = 100, COOLDOWN_MS = 900;
constexpr float SHAKE_DELTA_G = 0.55f;
bool available = false, eventPending = false;
uint8_t address = 0;
unsigned long lastSampleAt = 0, lastShakeAt = 0;
float previousMagnitude = 1.0f;
bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}
bool readBytes(uint8_t reg, uint8_t* buffer, size_t count) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  // Usa repeated start (false) per mantenere il controllo del bus e leggere i registri correttamente
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)address, (int)count) != count) return false;
  for (size_t i = 0; i < count; i++) buffer[i] = Wire.read();
  return true;
}
}

void accelerometerBegin() {
  // Il bus I2C (Wire) è già stato inizializzato nel main.cpp per l'OLED.
  uint8_t identity = 0;
  for (uint8_t candidate : ADDRESSES) {
    address = candidate;
    if (readBytes(REG_WHO_AM_I, &identity, 1) && (identity == 0x68 || identity == 0x69 || identity == 0x70)) break;
    address = 0;
  }
  if (!address) { networkManagerRecordLog("[ACC] MPU6050 non rilevato"); return; }
  available = writeReg(REG_POWER, 0x01) && writeReg(REG_ACCEL_CONFIG, 0x08);
  if (!available) { networkManagerRecordLog("[ACC] Errore inizializzazione"); return; }
  networkManagerRecordLog(String("[ACC] Pronto I2C 0x") + String(address, HEX) + " (polling)");
}

void accelerometerLoop() {
  if (!available) return;
  unsigned long now = millis();
  // Il polling lento è sufficiente per rilevare una scossa senza usare INT.
  if (now - lastSampleAt < SAMPLE_INTERVAL_MS) return;
  lastSampleAt = now;
  uint8_t raw[6];
  if (!readBytes(REG_ACCEL, raw, sizeof(raw))) return;
  int16_t x = (raw[0] << 8) | raw[1], y = (raw[2] << 8) | raw[3], z = (raw[4] << 8) | raw[5];
  float gx = x / 8192.0f, gy = y / 8192.0f, gz = z / 8192.0f;
  float magnitude = sqrtf(gx * gx + gy * gy + gz * gz);
  float delta = fabsf(magnitude - previousMagnitude); previousMagnitude = magnitude;
  if (delta >= SHAKE_DELTA_G && now - lastShakeAt >= COOLDOWN_MS) {
    lastShakeAt = now; eventPending = true; networkManagerRecordLog("[ACC] Scossa rilevata");
  }
}

bool accelerometerTakeShakeEvent() { bool result = eventPending; eventPending = false; return result; }
