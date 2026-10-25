#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace I2CScanner {
void scan(TwoWire& bus, const char* bus_label, Stream& out);
}
