#include "Dashboard.h"

#include <math.h>

namespace {

const char* kRule = "==================================================";

void row(Stream& out, const char* label, const char* value) {
  out.printf("%-16s: %s\n", label, value);
}

void valueText(char* buf, size_t cap, float v, SensorStatus st,
               const char* unit, int decimals) {
  switch (st) {
    case SensorStatus::NotPresent:
      snprintf(buf, cap, "---");
      return;
    case SensorStatus::ReadError:
      snprintf(buf, cap, "ERROR (read failed)");
      return;
    case SensorStatus::Saturated:
      snprintf(buf, cap, ">= %.0f %s  [SATURATED]", v, unit);
      return;
    case SensorStatus::Simulated:
      snprintf(buf, cap, "%.*f %s  [SIMULATED]", decimals, v, unit);
      return;
    case SensorStatus::Ok:
    default:
      if (isnan(v)) {
        snprintf(buf, cap, "---");
      } else {
        snprintf(buf, cap, "%.*f %s", decimals, v, unit);
      }
      return;
  }
}

void printPanel(Stream& out, const char* heading, const PanelReading& p) {
  char buf[48];
  out.println();
  out.println(heading);
  valueText(buf, sizeof(buf), p.voltage, p.status, "V", 2);
  row(out, "Voltage", buf);
  valueText(buf, sizeof(buf), p.current, p.status, "A", 3);
  row(out, "Current", buf);
  valueText(buf, sizeof(buf), p.power, p.status, "W", 2);
  row(out, "Power", buf);
  valueText(buf, sizeof(buf), p.temperature, p.temperature_status, "C", 1);
  row(out, "Temperature", buf);
}

void printSystem(Stream& out, const SystemStatus& s) {
  char buf[80];

  out.println();
  out.println("SYSTEM");

  if (!s.wifi_configured) {
    row(out, "WiFi", "NOT CONFIGURED (set credentials in secrets.h)");
  } else if (s.wifi_connected) {
    snprintf(buf, sizeof(buf), "CONNECTED  %s  (%d dBm)", s.ip, s.rssi);
    row(out, "WiFi", buf);
  } else {
    row(out, "WiFi", "DISCONNECTED (retrying, acquisition unaffected)");
  }

  if (!s.backend_configured) {
    row(out, "Backend", "NOT CONFIGURED");
  } else {
    snprintf(buf, sizeof(buf), "%s  http=%d  ok=%lu fail=%lu",
             backendResultName(s.last_upload), s.last_http_code,
             (unsigned long)s.uploads_ok, (unsigned long)s.uploads_failed);
    row(out, "Backend", buf);
  }

  if (s.sd_available) {
    snprintf(buf, sizeof(buf), "OK  %lu rows written",
             (unsigned long)s.sd_rows);
    row(out, "SD Card", buf);
  } else {
    row(out, "SD Card", "NOT CONNECTED (CSV echoed to Serial instead)");
  }

  snprintf(buf, sizeof(buf), "%lu pending, %lu dropped",
           (unsigned long)s.buffered, (unsigned long)s.buffer_dropped);
  row(out, "Upload buffer", buf);

  snprintf(buf, sizeof(buf), "%lu bytes", (unsigned long)s.free_heap);
  row(out, "Free heap", buf);
}

void printFull(Stream& out, const SensorData& d, const SystemStatus& s) {
  char buf[80];
  char ts[40];

  out.println();
  out.println(kRule);
  out.println("                 SOLARSENSE");
  out.println(kRule);
  out.println();

  formatTimestamp(d, ts, sizeof(ts));
  snprintf(buf, sizeof(buf), "%s  [%s]", ts, timeSourceName(d.time_source));
  row(out, "Timestamp", buf);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)d.seq);
  row(out, "Sequence", buf);

  out.println();
  out.println("ENVIRONMENT");
  valueText(buf, sizeof(buf), d.light_lux, d.light_status, "lux", 0);
  row(out, "Light", buf);
  valueText(buf, sizeof(buf), d.ambient_temperature, d.ambient_status, "C", 1);
  row(out, "Ambient Temp", buf);
  valueText(buf, sizeof(buf), d.humidity, d.ambient_status, "%", 1);
  row(out, "Humidity", buf);
  valueText(buf, sizeof(buf), d.rainfall, d.rain_status, "mm", 2);
  row(out, "Rainfall", buf);

  printPanel(out, "PANEL A - CLEAN REFERENCE", d.panelA);
  printPanel(out, "PANEL B - NEVER CLEANED", d.panelB);
  printPanel(out, "PANEL C - WEEKLY CLEANED", d.panelC);

  out.println();
  out.println("COMPARISON");
  if (!d.comparison_valid) {
    row(out, "Status", "NOT COMPUTED");
    row(out, "Reason", d.comparison_note);
  } else {
    if (isnan(d.loss_B_pct)) {
      row(out, "Panel B Loss", "--- (panel B unavailable)");
    } else {
      snprintf(buf, sizeof(buf), "%.1f %%  (efficiency %.3f)", d.loss_B_pct,
               d.efficiency_B);
      row(out, "Panel B Loss", buf);
    }
    if (isnan(d.loss_C_pct)) {
      row(out, "Panel C Loss", "--- (panel C unavailable)");
    } else {
      snprintf(buf, sizeof(buf), "%.1f %%  (efficiency %.3f)", d.loss_C_pct,
               d.efficiency_C);
      row(out, "Panel C Loss", buf);
    }
    row(out, "Note", "relative power only; not yet a soiling diagnosis");
  }

  printSystem(out, s);
  out.println(kRule);
}

void printDemo(Stream& out, const SensorData& d, const SystemStatus& s) {
  char buf[64];

  out.println();
  out.println(kRule);
  out.println("            SOLARSENSE LIVE MONITOR");
  out.println(kRule);

  if (d.simulated) {
    out.println();
    out.println("  *** SIMULATION MODE - VALUES MARKED [SIM] ARE NOT REAL ***");
  }
  out.println();

  if (statusUsable(d.light_status)) {
    snprintf(buf, sizeof(buf), "%.1f klux%s", d.light_lux / 1000.0f,
             d.light_status == SensorStatus::Simulated ? "  [SIM]" : "");
  } else {
    snprintf(buf, sizeof(buf), "---");
  }
  row(out, "Sunlight", buf);

  const PanelReading* panels[3] = {&d.panelA, &d.panelB, &d.panelC};
  const char* names[3] = {"Panel A (clean)", "Panel B (never)",
                          "Panel C (weekly)"};
  for (int i = 0; i < 3; i++) {
    valueText(buf, sizeof(buf), panels[i]->power, panels[i]->status, "W", 2);
    row(out, names[i], buf);
  }

  out.println();
  out.println("Estimated Loss vs Clean Reference");
  if (!d.comparison_valid) {
    row(out, "Status", d.comparison_note);
  } else {
    if (isnan(d.loss_B_pct)) {
      row(out, "Never Cleaned", "---");
    } else {
      snprintf(buf, sizeof(buf), "%.1f %%", d.loss_B_pct);
      row(out, "Never Cleaned", buf);
    }
    if (isnan(d.loss_C_pct)) {
      row(out, "Weekly Cleaned", "---");
    } else {
      snprintf(buf, sizeof(buf), "%.1f %%", d.loss_C_pct);
      row(out, "Weekly Cleaned", buf);
    }
  }

  out.println();
  const char* state = "OFFLINE (logging locally)";
  if (s.wifi_connected && s.backend_configured &&
      s.last_upload == BackendResult::Sent) {
    state = "ONLINE";
  } else if (s.wifi_connected) {
    state = "ONLINE (backend not configured)";
  }
  row(out, "System Status", state);
  out.println(kRule);
}

}

void Dashboard::printBanner(Stream& out, const char* firmware,
                            const char* device_id) {
  out.println();
  out.println(kRule);
  out.println("                 SOLARSENSE");
  out.println("     Solar panel soiling monitoring station");
  out.println(kRule);
  out.printf("Device          : %s\n", device_id);
  out.printf("Firmware        : %s\n", firmware);
  out.println();
}

void Dashboard::printInitReport(Stream& out, const SystemStatus& s) {
  out.println();
  out.println("SENSOR INITIALISATION");
  out.println("---------------------");
  out.printf("BH1750             : %s\n", sensorStatusName(s.bh1750));
  out.printf("INA219 Panel A     : %s\n", sensorStatusName(s.ina_a));
  out.printf("INA219 Panel B     : %s\n", sensorStatusName(s.ina_b));
  out.printf("INA219 Panel C     : %s\n", sensorStatusName(s.ina_c));
  out.printf("DS3231             : %s%s\n",
             s.rtc_available ? "OK" : "NOT CONNECTED",
             s.rtc_available && s.rtc_lost_power ? "  (lost power - time not trusted)" : "");
  if (s.ds18b20_count == 0) {
    out.println("DS18B20            : NOT CONNECTED");
  } else {
    out.printf("DS18B20            : OK (%u of 3 found)%s\n", s.ds18b20_count,
               s.ds18b20_mapping_explicit
                   ? ""
                   : "  WARNING: panel mapping not set in config.h");
  }
  out.printf("DHT22              : %s\n",
             s.dht_available ? "OK" : "NOT CONNECTED");
  out.printf("microSD            : %s\n",
             s.sd_available ? "OK" : "NOT CONNECTED");
  out.printf("Rain gauge         : %s\n",
             s.rain_available ? "OK" : "NOT CONNECTED");
  out.printf("WiFi               : %s\n",
             !s.wifi_configured ? "NOT CONFIGURED"
                                : (s.wifi_connected ? "CONNECTED" : "FAILED"));
  out.printf("Backend            : %s\n",
             s.backend_configured ? "CONFIGURED" : "NOT CONFIGURED");
  out.printf("Time source        : %s\n", timeSourceName(s.time_source));
  out.println();
}

void Dashboard::print(Stream& out, const SensorData& d, const SystemStatus& s,
                      bool demo) {
  if (demo) {
    printDemo(out, d, s);
  } else {
    printFull(out, d, s);
  }
}

void Dashboard::printHelp(Stream& out) {
  out.println();
  out.println("SERIAL COMMANDS");
  out.println("  h  this help");
  out.println("  i  scan both I2C buses and list responding addresses");
  out.println("  r  force a sensor read and reprint the dashboard now");
  out.println("  j  print the exact JSON that would be POSTed");
  out.println("  c  print the CSV header and the latest row");
  out.println("  u  force a backend upload attempt now");
  out.println("  d  toggle demo view / full diagnostic view");
  out.println("  t  list DS18B20 ROM addresses found on the OneWire bus");
  out.println("  s  reprint the sensor initialisation report");
  out.println();
}
