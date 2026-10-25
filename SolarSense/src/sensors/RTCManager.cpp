#include "RTCManager.h"

static const time_t kPlausibleEpoch = 1672531200;

bool RTCManager::begin(TwoWire* bus) {
  available_ = false;
  lost_power_ = false;
  if (bus == nullptr) return false;

  if (!rtc_.begin(bus)) return false;

  available_ = true;
  lost_power_ = rtc_.lostPower();
  return true;
}

time_t RTCManager::now(TimeSource& source_out) {
  if (available_ && !lost_power_) {
    time_t t = (time_t)rtc_.now().unixtime();
    if (t > kPlausibleEpoch) {
      source_out = TimeSource::Rtc;
      return t;
    }
  }
  time_t sys = time(nullptr);
  if (ntp_synced_ && sys > kPlausibleEpoch) {
    source_out = TimeSource::Ntp;
    return sys;
  }
  source_out = TimeSource::Uptime;
  return 0;
}

bool RTCManager::syncRtcFromSystem() {
  if (!available_ || !ntp_synced_) return false;
  time_t sys = time(nullptr);
  if (sys < kPlausibleEpoch) return false;

  rtc_.adjust(DateTime((uint32_t)sys));
  lost_power_ = false;
  return true;
}
