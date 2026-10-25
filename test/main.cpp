#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>

#include "src/communication/BackendClient.h"
#include "src/data/DataProcessor.h"
#include "src/data/SensorData.h"
#include "src/data/SystemStatus.h"
#include "src/logging/RecordBuffer.h"
#include "src/logging/SDLogger.h"
#include "src/ui/Dashboard.h"

SDClass SD;
WiFiShim WiFi;
unsigned long millis() { return 3615000UL; }

static Stream Out;
static int g_failures = 0;

static void check(const char* what, bool ok) {
  printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
  if (!ok) g_failures++;
}

static DataProcessor makeProcessor() {
  DataProcessor::Config cfg = {20000.0f, 0.5f};
  return DataProcessor(cfg);
}

static SensorData currentHardwareRecord() {
  SensorData d;
  d.seq = 42;
  d.uptime_ms = 3615000UL;
  d.timestamp = 1787241615;
  d.time_source = TimeSource::Ntp;

  d.light_lux = 82450.0f;
  d.light_status = SensorStatus::Ok;

  d.panelA.voltage = 18.42f;
  d.panelA.current = 1.240f;
  d.panelA.status = SensorStatus::Ok;

  d.panelB.voltage = 17.91f;
  d.panelB.current = 0.910f;
  d.panelB.status = SensorStatus::Ok;
  return d;
}

int main() {
  DataProcessor proc = makeProcessor();
  BackendClient backend;
  backend.begin("http://192.168.1.50:8000/api/sensors/data", "SOLARSENSE_01",
                "", "0.5.0", 4000);

  SystemStatus st;
  st.bh1750 = SensorStatus::Ok;
  st.ina_a = SensorStatus::Ok;
  st.ina_b = SensorStatus::Ok;
  st.ina_c = SensorStatus::NotPresent;
  st.wifi_configured = true;
  st.wifi_connected = true;
  strcpy(st.ip, "192.168.1.42");
  st.rssi = -54;
  st.backend_configured = true;
  st.last_upload = BackendResult::Sent;
  st.last_http_code = 201;
  st.uploads_ok = 7;
  st.time_source = TimeSource::Ntp;
  st.free_heap = 241536;

  SensorData d = currentHardwareRecord();
  proc.process(d);

  printf("\n########## 1. FULL DASHBOARD (today's hardware) ##########\n");
  Dashboard::print(Out, d, st, false);

  printf("\n########## 2. DEMO VIEW ##########\n");
  Dashboard::print(Out, d, st, true);

  printf("\n########## 3. INIT REPORT ##########\n");
  Dashboard::printInitReport(Out, st);

  printf("\n########## 4. JSON PAYLOAD ##########\n");
  char json[1400];
  size_t n = backend.buildJson(d, json, sizeof(json));
  printf("%s\n(%u bytes)\n", json, (unsigned)n);

  printf("\n########## 5. CSV ##########\n");
  char row[512];
  printf("%s\n", SDLogger::csvHeader());
  SDLogger::buildCsvRow(d, row, sizeof(row));
  printf("%s\n", row);

  printf("\n########## 6. ASSERTIONS ##########\n");

  check("panelA power = 18.42 * 1.240 = 22.84 W",
        fabs(d.panelA.power - 22.8408f) < 0.001f);
  check("panelB power = 17.91 * 0.910 = 16.30 W",
        fabs(d.panelB.power - 16.2981f) < 0.001f);
  check("panelC power stays NAN when the sensor is absent", isnan(d.panelC.power));

  check("loss B is ~28.65%", fabs(d.loss_B_pct - 28.6493f) < 0.01f);
  check("loss C stays NAN with panel C absent", isnan(d.loss_C_pct));
  check("comparison is valid in full sun", d.comparison_valid);

  check("JSON fits the buffer", n > 0 && n < sizeof(json));
  check("absent panel C serialises as null, not 0",
        strstr(json, "\"panelC\":{\"voltage\":null,\"current\":null,\"power\":null") != NULL);
  check("absent panel C carries status NOT CONNECTED",
        strstr(json, "\"panelC\":{\"voltage\":null,\"current\":null,\"power\":null,\"temperature\":null,\"status\":\"NOT CONNECTED\"") != NULL);
  check("panelA power present in JSON", strstr(json, "\"power\":22.84") != NULL);
  check("simulated flag is false on real data",
        strstr(json, "\"simulated\":false") != NULL);
  check("device_id present", strstr(json, "\"device_id\":\"SOLARSENSE_01\"") != NULL);
  check("ISO timestamp present", strstr(json, "T21:30:15") != NULL);

  check("CSV panelC columns are empty, not 0",
        strstr(row, ",,,,NOT CONNECTED,") != NULL);

  SensorData dim = currentHardwareRecord();
  dim.light_lux = 4200.0f;
  proc.process(dim);
  check("low sunlight suppresses the loss figure", !dim.comparison_valid);
  check("low sunlight leaves loss NAN", isnan(dim.loss_B_pct));
  check("low sunlight explains itself",
        strstr(dim.comparison_note, "low sunlight") != NULL);
  check("power is still computed under low light",
        !isnan(dim.panelA.power) && !isnan(dim.panelB.power));

  SensorData dark = currentHardwareRecord();
  dark.panelA.current = 0.0f;
  proc.process(dark);
  check("zero reference power does not divide by zero",
        !dark.comparison_valid && isnan(dark.loss_B_pct));
  check("zero reference power is a real 0.0 W reading, not NAN",
        dark.panelA.power == 0.0f);

  SensorData noref = currentHardwareRecord();
  noref.panelA.status = SensorStatus::NotPresent;
  noref.panelA.voltage = NAN;
  noref.panelA.current = NAN;
  proc.process(noref);
  check("missing reference panel blocks the comparison",
        !noref.comparison_valid &&
            strstr(noref.comparison_note, "reference panel A unavailable") != NULL);

  SensorData nolight = currentHardwareRecord();
  nolight.light_status = SensorStatus::ReadError;
  nolight.light_lux = NAN;
  proc.process(nolight);
  check("no light reading blocks the comparison", !nolight.comparison_valid);

  SensorData sat = currentHardwareRecord();
  sat.light_status = SensorStatus::Saturated;
  sat.light_lux = 121556.0f;
  proc.process(sat);
  check("saturated light reading withholds the comparison",
        !sat.comparison_valid);

  RecordBuffer<3> buf;
  SensorData a = d, b = d, c = d, e = d;
  a.seq = 1; b.seq = 2; c.seq = 3; e.seq = 4;
  buf.push(a); buf.push(b); buf.push(c);
  check("buffer fills to capacity", buf.size() == 3 && buf.dropped() == 0);
  buf.push(e);
  SensorData front;
  buf.peek(front);
  check("overflow drops the oldest and counts it",
        buf.size() == 3 && buf.dropped() == 1 && front.seq == 2);
  buf.pop();
  buf.peek(front);
  check("pop advances oldest-first", front.seq == 3);

  printf("\n%d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
