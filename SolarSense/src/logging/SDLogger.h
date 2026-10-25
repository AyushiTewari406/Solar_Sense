#pragma once

#include <Arduino.h>

#include "../data/SensorData.h"

class SDLogger {
 public:
  bool begin(uint8_t cs_pin, const char* path);
  bool available() const { return available_; }
  const char* path() const { return path_; }
  uint32_t rowsWritten() const { return rows_written_; }
  uint64_t cardSizeMB() const { return card_size_mb_; }

  bool logRecord(const SensorData& d);

  static const char* csvHeader();
  static size_t buildCsvRow(const SensorData& d, char* out, size_t cap);

 private:
  bool writeHeaderIfNew();

  bool available_ = false;
  const char* path_ = "/solarsense.csv";
  uint8_t cs_pin_ = 5;
  uint32_t rows_written_ = 0;
  uint64_t card_size_mb_ = 0;
};
