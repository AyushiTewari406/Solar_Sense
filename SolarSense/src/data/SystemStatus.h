#pragma once

#include <Arduino.h>

#include "../communication/BackendClient.h"
#include "SensorData.h"

struct SystemStatus {
  SensorStatus bh1750 = SensorStatus::NotPresent;
  SensorStatus ina_a  = SensorStatus::NotPresent;
  SensorStatus ina_b  = SensorStatus::NotPresent;
  SensorStatus ina_c  = SensorStatus::NotPresent;

  bool rtc_available = false;
  bool rtc_lost_power = false;
  uint8_t ds18b20_count = 0;
  bool ds18b20_mapping_explicit = false;
  bool dht_available = false;
  bool sd_available = false;
  bool rain_available = false;

  bool wifi_configured = false;
  bool wifi_connected = false;
  char ip[16] = "0.0.0.0";
  int rssi = 0;

  bool backend_configured = false;
  BackendResult last_upload = BackendResult::NotConfigured;
  int last_http_code = 0;
  uint32_t uploads_ok = 0;
  uint32_t uploads_failed = 0;

  uint32_t buffered = 0;
  uint32_t buffer_dropped = 0;
  uint32_t sd_rows = 0;

  TimeSource time_source = TimeSource::Uptime;
  bool simulation = false;
  uint32_t free_heap = 0;
};
