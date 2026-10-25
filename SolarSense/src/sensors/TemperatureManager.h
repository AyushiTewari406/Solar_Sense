#pragma once

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>

#include "../data/SensorData.h"

class TemperatureManager {
 public:
  static constexpr uint8_t kPanelCount = 3;

  bool begin(uint8_t pin, uint8_t resolution_bits);

  bool available() const { return device_count_ > 0; }
  uint8_t deviceCount() const { return device_count_; }

  void assignPanel(uint8_t panel_index, const uint8_t rom[8]);
  bool mappingExplicit() const { return explicit_mapping_; }

  bool romString(uint8_t index, char* out, size_t cap) const;

  void requestAll();
  SensorStatus read(uint8_t panel_index, float& celsius);

 private:
  OneWire wire_;
  DallasTemperature sensors_{&wire_};
  uint8_t device_count_ = 0;
  bool explicit_mapping_ = false;
  bool requested_ = false;
  DeviceAddress discovered_[kPanelCount];
  DeviceAddress assigned_[kPanelCount];
  bool assigned_valid_[kPanelCount] = {false, false, false};
};
