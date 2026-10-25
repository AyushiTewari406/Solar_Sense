#include "BH1750Sensor.h"

#include <math.h>

static const float kBaseCeilingLux = 54612.0f;
static const uint8_t kDefaultMtreg = 69;

bool BH1750Sensor::begin(TwoWire* bus, uint8_t address, uint8_t mtreg) {
  bus_ = bus;
  address_ = address;
  available_ = false;

  if (bus_ == nullptr) return false;

  if (!sensor_.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, address_, bus_)) {
    return false;
  }
  if (mtreg != kDefaultMtreg) {
    sensor_.setMTreg(mtreg);
  }
  ceiling_ = kBaseCeilingLux * (float)kDefaultMtreg / (float)mtreg;
  available_ = true;
  return true;
}

SensorStatus BH1750Sensor::read(float& lux) {
  lux = NAN;
  if (!available_) return SensorStatus::NotPresent;

  float value = sensor_.readLightLevel();
  if (value < 0.0f || isnan(value)) {
    return SensorStatus::ReadError;
  }
  if (value >= ceiling_ * 0.99f) {
    lux = ceiling_;
    return SensorStatus::Saturated;
  }
  lux = value;
  return SensorStatus::Ok;
}
