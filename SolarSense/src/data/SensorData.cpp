#include "SensorData.h"

const char* sensorStatusName(SensorStatus s) {
  switch (s) {
    case SensorStatus::Ok:         return "OK";
    case SensorStatus::ReadError:  return "READ ERROR";
    case SensorStatus::Saturated:  return "SATURATED";
    case SensorStatus::Simulated:  return "SIMULATED";
    case SensorStatus::NotPresent:
    default:                       return "NOT CONNECTED";
  }
}

bool statusUsable(SensorStatus s) {
  return s == SensorStatus::Ok || s == SensorStatus::Simulated;
}

const char* timeSourceName(TimeSource s) {
  switch (s) {
    case TimeSource::Rtc: return "DS3231";
    case TimeSource::Ntp: return "NTP";
    case TimeSource::Uptime:
    default:              return "UPTIME";
  }
}

static void formatUptime(const SensorData& d, char* out, size_t cap) {
  unsigned long total = d.uptime_ms / 1000UL;
  snprintf(out, cap, "T+%02lu:%02lu:%02lu (no clock)",
           total / 3600UL, (total / 60UL) % 60UL, total % 60UL);
}

void formatTimestamp(const SensorData& d, char* out, size_t cap) {
  if (d.time_source == TimeSource::Uptime || d.timestamp == 0) {
    formatUptime(d, out, cap);
    return;
  }
  struct tm tm_info;
  time_t t = d.timestamp;
  localtime_r(&t, &tm_info);
  strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tm_info);
}

void formatTimestampIso(const SensorData& d, char* out, size_t cap) {
  if (d.time_source == TimeSource::Uptime || d.timestamp == 0) {
    snprintf(out, cap, "uptime:%lu", d.uptime_ms / 1000UL);
    return;
  }
  struct tm tm_info;
  time_t t = d.timestamp;
  localtime_r(&t, &tm_info);
  strftime(out, cap, "%Y-%m-%dT%H:%M:%S", &tm_info);
}
