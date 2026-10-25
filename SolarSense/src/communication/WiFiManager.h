#pragma once

#include <Arduino.h>

class WiFiLink {
 public:
  bool begin(const char* ssid, const char* password, unsigned long timeout_ms);

  bool configured() const { return configured_; }
  bool connected() const;

  void loop();

  const char* ip() const { return ip_; }
  int rssi() const;

  bool syncTime(const char* ntp_server, long tz_offset_sec, int dst_offset_sec,
                unsigned long timeout_ms);
  bool timeSynced() const { return time_synced_; }

 private:
  const char* ssid_ = nullptr;
  const char* password_ = nullptr;
  bool configured_ = false;
  bool time_synced_ = false;
  char ip_[16] = "0.0.0.0";
  unsigned long last_reconnect_attempt_ms_ = 0;
};
