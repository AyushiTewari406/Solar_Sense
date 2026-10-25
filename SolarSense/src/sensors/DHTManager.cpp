#include "DHTManager.h"

#include <math.h>

bool DHTManager::begin(uint8_t pin, uint8_t type) {
  if (dev_ == nullptr) dev_ = new DHT(pin, type);
  dev_->begin();

  delay(1200);
  float t = dev_->readTemperature();
  available_ = !isnan(t);
  return available_;
}

SensorStatus DHTManager::read(float& celsius, float& humidity_pct) {
  celsius = NAN;
  humidity_pct = NAN;
  if (!available_) return SensorStatus::NotPresent;

  float t = dev_->readTemperature();
  float h = dev_->readHumidity();
  if (isnan(t) || isnan(h)) return SensorStatus::ReadError;

  celsius = t;
  humidity_pct = h;
  return SensorStatus::Ok;
}
