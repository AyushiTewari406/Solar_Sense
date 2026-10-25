#include "INA219Panel.h"

#include <math.h>

INA219Panel::~INA219Panel() {
  delete dev_;
}

bool INA219Panel::probe() {
  if (bus_ == nullptr) return false;
  bus_->beginTransmission(address_);
  return bus_->endTransmission() == 0;
}

bool INA219Panel::begin(TwoWire* bus, uint8_t address, Range range) {
  bus_ = bus;
  address_ = address;
  available_ = false;

  if (!probe()) return false;

  if (dev_ == nullptr) dev_ = new Adafruit_INA219(address_);
  if (!dev_->begin(bus_)) return false;

  switch (range) {
    case RANGE_32V_1A:    dev_->setCalibration_32V_1A(); break;
    case RANGE_16V_400MA: dev_->setCalibration_16V_400mA(); break;
    case RANGE_32V_2A:
    default:              dev_->setCalibration_32V_2A(); break;
  }
  available_ = true;
  return true;
}

const char* INA219Panel::busName() const {
  if (bus_ == &Wire) return "Wire";
  if (bus_ == &Wire1) return "Wire1";
  return "?";
}

SensorStatus INA219Panel::read(PanelReading& out) {
  out.voltage = NAN;
  out.current = NAN;
  out.power = NAN;

  if (!available_) {
    out.status = SensorStatus::NotPresent;
    return out.status;
  }
  if (!probe()) {
    out.status = SensorStatus::ReadError;
    return out.status;
  }

  float bus_v = dev_->getBusVoltage_V();
  float shunt_mv = dev_->getShuntVoltage_mV();
  float current_ma = dev_->getCurrent_mA();

  if (isnan(bus_v) || isnan(shunt_mv) || isnan(current_ma)) {
    out.status = SensorStatus::ReadError;
    return out.status;
  }

  out.voltage = bus_v + (shunt_mv / 1000.0f);
  out.current = current_ma / 1000.0f;
  out.status = SensorStatus::Ok;
  return out.status;
}
