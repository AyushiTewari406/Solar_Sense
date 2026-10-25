#include "SDLogger.h"

#include <SD.h>
#include <SPI.h>
#include <math.h>

const char* SDLogger::csvHeader() {
  return "timestamp,time_source,seq,light_lux,light_status,"
         "panelA_voltage,panelA_current,panelA_power,panelA_status,"
         "panelB_voltage,panelB_current,panelB_power,panelB_status,"
         "panelC_voltage,panelC_current,panelC_power,panelC_status,"
         "panelA_temp,panelB_temp,panelC_temp,"
         "ambient_temp,humidity,rainfall_mm,"
         "loss_B_pct,loss_C_pct,comparison_valid,simulated";
}

namespace {
void fmtNum(char* out, size_t cap, float v, int decimals) {
  if (isnan(v)) {
    out[0] = '\0';
  } else {
    snprintf(out, cap, "%.*f", decimals, v);
  }
}
}

size_t SDLogger::buildCsvRow(const SensorData& d, char* out, size_t cap) {
  char ts[32];
  formatTimestamp(d, ts, sizeof(ts));

  char lux[16], av[12], ac[12], ap[12], bv[12], bc[12], bp[12];
  char cv[12], cc[12], cp[12], ta[10], tb[10], tc[10];
  char amb[10], hum[10], rain[12], lb[10], lc[10];

  fmtNum(lux, sizeof(lux), d.light_lux, 1);
  fmtNum(av, sizeof(av), d.panelA.voltage, 3);
  fmtNum(ac, sizeof(ac), d.panelA.current, 4);
  fmtNum(ap, sizeof(ap), d.panelA.power, 3);
  fmtNum(bv, sizeof(bv), d.panelB.voltage, 3);
  fmtNum(bc, sizeof(bc), d.panelB.current, 4);
  fmtNum(bp, sizeof(bp), d.panelB.power, 3);
  fmtNum(cv, sizeof(cv), d.panelC.voltage, 3);
  fmtNum(cc, sizeof(cc), d.panelC.current, 4);
  fmtNum(cp, sizeof(cp), d.panelC.power, 3);
  fmtNum(ta, sizeof(ta), d.panelA.temperature, 2);
  fmtNum(tb, sizeof(tb), d.panelB.temperature, 2);
  fmtNum(tc, sizeof(tc), d.panelC.temperature, 2);
  fmtNum(amb, sizeof(amb), d.ambient_temperature, 2);
  fmtNum(hum, sizeof(hum), d.humidity, 1);
  fmtNum(rain, sizeof(rain), d.rainfall, 3);
  fmtNum(lb, sizeof(lb), d.loss_B_pct, 2);
  fmtNum(lc, sizeof(lc), d.loss_C_pct, 2);

  int written = snprintf(
      out, cap,
      "%s,%s,%lu,%s,%s,"
      "%s,%s,%s,%s,"
      "%s,%s,%s,%s,"
      "%s,%s,%s,%s,"
      "%s,%s,%s,"
      "%s,%s,%s,"
      "%s,%s,%d,%d",
      ts, timeSourceName(d.time_source), (unsigned long)d.seq, lux,
      sensorStatusName(d.light_status),
      av, ac, ap, sensorStatusName(d.panelA.status),
      bv, bc, bp, sensorStatusName(d.panelB.status),
      cv, cc, cp, sensorStatusName(d.panelC.status),
      ta, tb, tc,
      amb, hum, rain,
      lb, lc, d.comparison_valid ? 1 : 0, d.simulated ? 1 : 0);

  if (written < 0 || (size_t)written >= cap) return 0;
  return (size_t)written;
}

bool SDLogger::begin(uint8_t cs_pin, const char* path) {
  cs_pin_ = cs_pin;
  path_ = path;
  available_ = false;

  if (!SD.begin(cs_pin_)) return false;
  if (SD.cardType() == CARD_NONE) return false;

  card_size_mb_ = SD.cardSize() / (1024ULL * 1024ULL);
  available_ = true;
  return writeHeaderIfNew();
}

bool SDLogger::writeHeaderIfNew() {
  if (SD.exists(path_)) return true;

  File f = SD.open(path_, FILE_WRITE);
  if (!f) {
    available_ = false;
    return false;
  }
  f.println(csvHeader());
  f.close();
  return true;
}

bool SDLogger::logRecord(const SensorData& d) {
  if (!available_) return false;

  static char row[512];
  if (buildCsvRow(d, row, sizeof(row)) == 0) return false;

  File f = SD.open(path_, FILE_APPEND);
  if (!f) return false;

  bool ok = f.println(row) > 0;
  f.close();
  if (ok) rows_written_++;
  return ok;
}
