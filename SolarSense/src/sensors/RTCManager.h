#pragma once

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>

#include "../data/SensorData.h"

class RTCManager {
 public:
  bool begin(TwoWire* bus);

  bool available() const { return available_; }
  bool lostPower() const { return lost_power_; }

  void markNtpSynced() { ntp_synced_ = true; }
  bool ntpSynced() const { return ntp_synced_; }

  time_t now(TimeSource& source_out);

  bool syncRtcFromSystem();

 private:
  RTC_DS3231 rtc_;
  bool available_ = false;
  bool lost_power_ = false;
  bool ntp_synced_ = false;
};
