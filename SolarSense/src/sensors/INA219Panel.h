#pragma once

#include <Adafruit_INA219.h>
#include <Arduino.h>
#include <Wire.h>

#include "../data/SensorData.h"

class INA219Panel {
 public:
  enum Range { RANGE_32V_2A, RANGE_32V_1A, RANGE_16V_400MA };

  explicit INA219Panel(const char* name) : name_(name) {}
  ~INA219Panel();

  bool begin(TwoWire* bus, uint8_t address, Range range);

  bool available() const { return available_; }
  const char* name() const { return name_; }
  uint8_t address() const { return address_; }
  const char* busName() const;

  SensorStatus read(PanelReading& out);

 private:
  bool probe();

  const char* name_;
  Adafruit_INA219* dev_ = nullptr;
  TwoWire* bus_ = nullptr;
  uint8_t address_ = 0x40;
  bool available_ = false;
};
