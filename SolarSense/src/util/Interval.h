#pragma once

#include <Arduino.h>

class Interval {
 public:
  explicit Interval(unsigned long period_ms) : period_ms_(period_ms) {}

  bool due() {
    unsigned long now = millis();
    if (first_ || (now - last_ms_) >= period_ms_) {
      last_ms_ = now;
      first_ = false;
      return true;
    }
    return false;
  }

  void trigger() { first_ = true; }

  void setPeriod(unsigned long period_ms) { period_ms_ = period_ms; }
  unsigned long period() const { return period_ms_; }

 private:
  unsigned long period_ms_;
  unsigned long last_ms_ = 0;
  bool first_ = true;
};
