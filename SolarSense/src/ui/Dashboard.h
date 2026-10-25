#pragma once

#include <Arduino.h>

#include "../data/SensorData.h"
#include "../data/SystemStatus.h"

namespace Dashboard {

void printBanner(Stream& out, const char* firmware, const char* device_id);

void print(Stream& out, const SensorData& d, const SystemStatus& s, bool demo);

void printInitReport(Stream& out, const SystemStatus& s);

void printHelp(Stream& out);

}
