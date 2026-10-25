#include "RainGauge.h"

#include <math.h>

static volatile uint32_t g_tips = 0;
static volatile unsigned long g_last_tip_ms = 0;

static const unsigned long kDebounceMs = 120;

static void IRAM_ATTR onTip() {
  unsigned long now = millis();
  if (now - g_last_tip_ms < kDebounceMs) return;
  g_last_tip_ms = now;
  g_tips++;
}

bool RainGauge::begin(uint8_t pin, float mm_per_tip) {
  mm_per_tip_ = mm_per_tip;
  pinMode(pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin), onTip, FALLING);
  available_ = true;
  return true;
}

SensorStatus RainGauge::read(float& mm_since_last, float& mm_total) {
  mm_since_last = NAN;
  mm_total = NAN;
  if (!available_) return SensorStatus::NotPresent;

  noInterrupts();
  uint32_t tips = g_tips;
  interrupts();

  mm_since_last = (tips - last_reported_tips_) * mm_per_tip_;
  mm_total = tips * mm_per_tip_;
  last_reported_tips_ = tips;
  return SensorStatus::Ok;
}
