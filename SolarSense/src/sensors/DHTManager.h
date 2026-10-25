#pragma once

#include <Arduino.h>
#include <DHT.h>

#include "../data/SensorData.h"

class DHTManager {
 public:
  bool begin(uint8_t pin, uint8_t type);
  bool available() const { return available_; }

  SensorStatus read(float& celsius, float& humidity_pct);

 private:
  DHT* dev_ = nullptr;
  bool available_ = false;
};
