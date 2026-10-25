#pragma once

#include <Arduino.h>
#include <time.h>

enum class SensorStatus : uint8_t {
  NotPresent = 0,
  Ok,
  ReadError,
  Saturated,
  Simulated
};

const char* sensorStatusName(SensorStatus s);
bool statusUsable(SensorStatus s);

enum class TimeSource : uint8_t { Uptime = 0, Ntp, Rtc };
const char* timeSourceName(TimeSource s);

struct PanelReading {
  float voltage = NAN;
  float current = NAN;
  float power   = NAN;
  SensorStatus status = SensorStatus::NotPresent;

  float temperature = NAN;
  SensorStatus temperature_status = SensorStatus::NotPresent;
};

struct SensorData {
  uint32_t seq = 0;
  unsigned long uptime_ms = 0;
  time_t timestamp = 0;
  TimeSource time_source = TimeSource::Uptime;

  float light_lux = NAN;
  SensorStatus light_status = SensorStatus::NotPresent;

  PanelReading panelA;
  PanelReading panelB;
  PanelReading panelC;

  float ambient_temperature = NAN;
  float humidity = NAN;
  SensorStatus ambient_status = SensorStatus::NotPresent;

  float rainfall = NAN;
  float rainfall_total = NAN;
  SensorStatus rain_status = SensorStatus::NotPresent;

  float efficiency_B = NAN;
  float efficiency_C = NAN;
  float loss_B_pct = NAN;
  float loss_C_pct = NAN;
  bool comparison_valid = false;
  const char* comparison_note = "not computed";

  bool simulated = false;
};

void formatTimestamp(const SensorData& d, char* out, size_t cap);
void formatTimestampIso(const SensorData& d, char* out, size_t cap);
