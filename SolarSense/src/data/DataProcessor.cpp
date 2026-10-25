#include "DataProcessor.h"

#include <math.h>

static void computePower(PanelReading& p) {
  if (!statusUsable(p.status) || isnan(p.voltage) || isnan(p.current)) {
    p.power = NAN;
    return;
  }
  p.power = p.voltage * p.current;
}

void DataProcessor::process(SensorData& d) const {
  computePower(d.panelA);
  computePower(d.panelB);
  computePower(d.panelC);

  d.efficiency_B = NAN;
  d.efficiency_C = NAN;
  d.loss_B_pct = NAN;
  d.loss_C_pct = NAN;
  d.comparison_valid = false;

  if (!statusUsable(d.panelA.status) || isnan(d.panelA.power)) {
    d.comparison_note = "reference panel A unavailable";
    return;
  }
  if (d.panelA.power < cfg_.min_reference_power) {
    d.comparison_note = "reference output too low to compare";
    return;
  }
  if (!statusUsable(d.light_status) || isnan(d.light_lux)) {
    d.comparison_note = "no light reading - cannot separate cloud from soiling";
    return;
  }
  if (d.light_lux < cfg_.min_lux) {
    d.comparison_note = "low sunlight - difference not attributable to soiling";
    return;
  }

  d.comparison_valid = true;
  d.comparison_note = "valid";

  if (statusUsable(d.panelB.status) && !isnan(d.panelB.power)) {
    d.efficiency_B = d.panelB.power / d.panelA.power;
    d.loss_B_pct = 100.0f * (1.0f - d.efficiency_B);
  }
  if (statusUsable(d.panelC.status) && !isnan(d.panelC.power)) {
    d.efficiency_C = d.panelC.power / d.panelA.power;
    d.loss_C_pct = 100.0f * (1.0f - d.efficiency_C);
  }
}
