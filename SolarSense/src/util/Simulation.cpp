#include "Simulation.h"

#include <math.h>

namespace {

float wave(float centre, float amplitude, float phase) {
  float t = (float)(millis() % 120000UL) / 120000.0f;
  return centre + amplitude * sinf(2.0f * PI * t + phase);
}

void fillPanel(PanelReading& p, float voltage, float current) {
  if (p.status != SensorStatus::NotPresent) return;
  p.voltage = voltage;
  p.current = current;
  p.status = SensorStatus::Simulated;
}

}

void Simulation::fill(SensorData& d) {
  bool touched = false;

  if (d.light_status == SensorStatus::NotPresent) {
    d.light_lux = wave(82000.0f, 6000.0f, 0.0f);
    d.light_status = SensorStatus::Simulated;
    touched = true;
  }

  float ref_v = wave(18.4f, 0.3f, 0.0f);
  float ref_i = wave(1.24f, 0.08f, 0.0f);

  if (d.panelA.status == SensorStatus::NotPresent) {
    fillPanel(d.panelA, ref_v, ref_i);
    touched = true;
  }
  if (d.panelB.status == SensorStatus::NotPresent) {
    fillPanel(d.panelB, ref_v * 0.973f, ref_i * 0.741f);
    touched = true;
  }
  if (d.panelC.status == SensorStatus::NotPresent) {
    fillPanel(d.panelC, ref_v * 0.985f, ref_i * 0.870f);
    touched = true;
  }

  if (d.panelA.temperature_status == SensorStatus::NotPresent) {
    d.panelA.temperature = wave(45.2f, 1.5f, 0.0f);
    d.panelA.temperature_status = SensorStatus::Simulated;
    d.panelB.temperature = wave(52.7f, 1.5f, 0.3f);
    d.panelB.temperature_status = SensorStatus::Simulated;
    d.panelC.temperature = wave(48.1f, 1.5f, 0.6f);
    d.panelC.temperature_status = SensorStatus::Simulated;
    touched = true;
  }

  if (d.ambient_status == SensorStatus::NotPresent) {
    d.ambient_temperature = wave(29.4f, 1.0f, 0.0f);
    d.humidity = wave(68.2f, 4.0f, 1.0f);
    d.ambient_status = SensorStatus::Simulated;
    touched = true;
  }

  if (d.rain_status == SensorStatus::NotPresent) {
    d.rainfall = 0.0f;
    d.rainfall_total = 0.0f;
    d.rain_status = SensorStatus::Simulated;
    touched = true;
  }

  if (touched) d.simulated = true;
}
