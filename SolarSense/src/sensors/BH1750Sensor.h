#pragma once

#include <Arduino.h>
#include <BH1750.h>
#include <Wire.h>

#include "../data/SensorData.h"

class BH1750Sensor {
 public:
  bool begin(TwoWire* bus, uint8_t address, uint8_t mtreg);

  bool available() const { return available_; }

  SensorStatus read(float& lux);

  float saturationCeiling() const { return ceiling_; }

 private:
  BH1750 sensor_{0x23};
  TwoWire* bus_ = nullptr;
  uint8_t address_ = 0x23;
  bool available_ = false;
  float ceiling_ = 54612.0f;
};
