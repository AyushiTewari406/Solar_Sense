#pragma once

#include "SensorData.h"

class DataProcessor {
 public:
  struct Config {
    float min_lux;
    float min_reference_power;
  };

  explicit DataProcessor(const Config& cfg) : cfg_(cfg) {}

  void process(SensorData& d) const;

 private:
  Config cfg_;
};
