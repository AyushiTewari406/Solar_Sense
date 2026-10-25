#pragma once

#include <Arduino.h>

#include "../data/SensorData.h"

class RainGauge {
 public:
  bool begin(uint8_t pin, float mm_per_tip);
  bool available() const { return available_; }

  SensorStatus read(float& mm_since_last, float& mm_total);

 private:
  bool available_ = false;
  float mm_per_tip_ = 0.0f;
  uint32_t last_reported_tips_ = 0;
};
