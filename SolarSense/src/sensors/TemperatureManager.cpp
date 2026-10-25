#include "TemperatureManager.h"

#include <math.h>

static bool romIsZero(const uint8_t rom[8]) {
  for (uint8_t i = 0; i < 8; i++) {
    if (rom[i] != 0) return false;
  }
  return true;
}

bool TemperatureManager::begin(uint8_t pin, uint8_t resolution_bits) {
  wire_.begin(pin);
  sensors_.begin();
  sensors_.setWaitForConversion(false);

  uint8_t found = sensors_.getDeviceCount();
  device_count_ = found > kPanelCount ? kPanelCount : found;

  for (uint8_t i = 0; i < device_count_; i++) {
    if (sensors_.getAddress(discovered_[i], i)) {
      sensors_.setResolution(discovered_[i], resolution_bits);
    }
  }
  return device_count_ > 0;
}

void TemperatureManager::assignPanel(uint8_t panel_index, const uint8_t rom[8]) {
  if (panel_index >= kPanelCount) return;
  if (romIsZero(rom)) {
    assigned_valid_[panel_index] = false;
  } else {
    memcpy(assigned_[panel_index], rom, 8);
    assigned_valid_[panel_index] = true;
  }
  explicit_mapping_ = assigned_valid_[0] && assigned_valid_[1] && assigned_valid_[2];
}

bool TemperatureManager::romString(uint8_t index, char* out, size_t cap) const {
  if (index >= device_count_ || cap < 17) return false;
  for (uint8_t i = 0; i < 8; i++) {
    snprintf(out + i * 2, cap - i * 2, "%02X", discovered_[index][i]);
  }
  return true;
}

void TemperatureManager::requestAll() {
  if (device_count_ == 0) return;
  sensors_.requestTemperatures();
  requested_ = true;
}

SensorStatus TemperatureManager::read(uint8_t panel_index, float& celsius) {
  celsius = NAN;
  if (panel_index >= kPanelCount) return SensorStatus::NotPresent;
  if (device_count_ == 0 || !requested_) return SensorStatus::NotPresent;

  const uint8_t* rom = nullptr;
  if (assigned_valid_[panel_index]) {
    rom = assigned_[panel_index];
  } else if (panel_index < device_count_) {
    rom = discovered_[panel_index];
  } else {
    return SensorStatus::NotPresent;
  }

  float value = sensors_.getTempC((uint8_t*)rom);
  if (value == DEVICE_DISCONNECTED_C || isnan(value)) {
    return SensorStatus::ReadError;
  }
  celsius = value;
  return SensorStatus::Ok;
}
