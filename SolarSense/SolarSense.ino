#include <Wire.h>

#include "config.h"
#include "src/communication/BackendClient.h"
#include "src/communication/WiFiManager.h"
#include "src/data/DataProcessor.h"
#include "src/data/SensorData.h"
#include "src/data/SystemStatus.h"
#include "src/logging/RecordBuffer.h"
#include "src/logging/SDLogger.h"
#include "src/sensors/BH1750Sensor.h"
#include "src/sensors/DHTManager.h"
#include "src/sensors/I2CScanner.h"
#include "src/sensors/INA219Panel.h"
#include "src/sensors/RTCManager.h"
#include "src/sensors/RainGauge.h"
#include "src/sensors/TemperatureManager.h"
#include "src/ui/Dashboard.h"
#include "src/util/Interval.h"
#include "src/util/Simulation.h"

static BH1750Sensor g_light;
static INA219Panel g_panelA("Panel A");
static INA219Panel g_panelB("Panel B");
static INA219Panel g_panelC("Panel C");
static RTCManager g_rtc;
static TemperatureManager g_temps;
static DHTManager g_dht;
static RainGauge g_rain;
static WiFiLink g_wifi;
static BackendClient g_backend;
static SDLogger g_sd;
static RecordBuffer<UPLOAD_BUFFER_SIZE> g_pending;

static DataProcessor g_processor({MIN_LUX_FOR_COMPARISON,
                                  MIN_REFERENCE_POWER_W});

static SensorData g_latest;
static SystemStatus g_status;
static uint32_t g_seq = 0;
static bool g_demo_view = DEMO_MODE;

static Interval g_sensor_tick(SENSOR_INTERVAL_MS);
static Interval g_dashboard_tick(DASHBOARD_INTERVAL_MS);
static Interval g_sdlog_tick(SD_LOG_INTERVAL_MS);
static Interval g_upload_tick(UPLOAD_INTERVAL_MS);
static Interval g_wifi_tick(WIFI_CHECK_INTERVAL_MS);

static TwoWire* busFor(int selector);
static void initSensors();
static void initNetwork();
static void acquire();
static void logRecord(const SensorData& d);
static void attemptUpload();
static void drainBacklog();
static void handleSerialCommand(char c);
static void printJson(const SensorData& d);
static void printCsv(const SensorData& d);
static void printOneWireRoms();

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  Dashboard::printBanner(Serial, FIRMWARE_VERSION, DEVICE_ID);

  Wire.begin(I2C0_SDA_PIN, I2C0_SCL_PIN, I2C_CLOCK_HZ);
  Wire1.begin(I2C1_SDA_PIN, I2C1_SCL_PIN, I2C_CLOCK_HZ);

  I2CScanner::scan(Wire, "Wire  (SDA 21 / SCL 22)", Serial);
  I2CScanner::scan(Wire1, "Wire1 (SDA 16 / SCL 17)", Serial);

  initSensors();
  initNetwork();

  Dashboard::printInitReport(Serial, g_status);

  if (!PANEL_C_ENABLED) {
    Serial.println("NOTE: Panel C is disabled in config.h. All three INA219");
    Serial.println("      boards are still at address 0x40, so C cannot share");
    Serial.println("      a bus with A or B. Bridge A0 on B and A1 on C, then");
    Serial.println("      set INA219_ADDRESSES_SOLDERED true.");
    Serial.println();
  }

#if SIMULATION_MODE
  Serial.println("*** SIMULATION MODE IS ON ***");
  Serial.println("Absent sensors will be filled with fabricated values,");
  Serial.println("tagged [SIMULATED] everywhere they appear.");
#if !SIMULATION_ALLOW_UPLOAD
  Serial.println("Backend uploads are suppressed so fabricated data cannot");
  Serial.println("reach the real database.");
#endif
  Serial.println();
#endif

#if SERIAL_CSV_ECHO
  if (!g_sd.available()) {
    Serial.println("CSV LOG (microSD absent - rows follow the header below)");
    Serial.println(SDLogger::csvHeader());
  }
#endif

  Dashboard::printHelp(Serial);
}

void loop() {
  while (Serial.available() > 0) {
    handleSerialCommand((char)Serial.read());
  }

  if (g_wifi_tick.due()) {
    g_wifi.loop();
    g_status.wifi_connected = g_wifi.connected();
    g_status.rssi = g_wifi.rssi();
    strncpy(g_status.ip, g_wifi.ip(), sizeof(g_status.ip) - 1);
  }

  if (g_sensor_tick.due()) {
    acquire();
  }

  if (g_dashboard_tick.due()) {
    Dashboard::print(Serial, g_latest, g_status, g_demo_view);
  }

  if (g_sdlog_tick.due()) {
    logRecord(g_latest);
  }

  if (g_upload_tick.due()) {
    attemptUpload();
  }
}

static TwoWire* busFor(int selector) {
  return selector == BUS_WIRE1 ? &Wire1 : &Wire;
}

static void initSensors() {
  g_status.bh1750 = g_light.begin(&Wire, BH1750_I2C_ADDRESS, BH1750_MTREG)
                        ? SensorStatus::Ok
                        : SensorStatus::NotPresent;

#if PANEL_A_ENABLED
  g_status.ina_a = g_panelA.begin(busFor(PANEL_A_BUS), PANEL_A_ADDRESS,
                                  INA219_RANGE)
                       ? SensorStatus::Ok
                       : SensorStatus::NotPresent;
#endif
#if PANEL_B_ENABLED
  g_status.ina_b = g_panelB.begin(busFor(PANEL_B_BUS), PANEL_B_ADDRESS,
                                  INA219_RANGE)
                       ? SensorStatus::Ok
                       : SensorStatus::NotPresent;
#endif
#if PANEL_C_ENABLED
  g_status.ina_c = g_panelC.begin(busFor(PANEL_C_BUS), PANEL_C_ADDRESS,
                                  INA219_RANGE)
                       ? SensorStatus::Ok
                       : SensorStatus::NotPresent;
#endif

#if ENABLE_RTC
  g_status.rtc_available = g_rtc.begin(&Wire);
  g_status.rtc_lost_power = g_rtc.lostPower();
#endif

#if ENABLE_DS18B20
  g_temps.begin(DS18B20_PIN, DS18B20_RESOLUTION);
  g_temps.assignPanel(0, DS18B20_ROM_PANEL_A);
  g_temps.assignPanel(1, DS18B20_ROM_PANEL_B);
  g_temps.assignPanel(2, DS18B20_ROM_PANEL_C);
  g_status.ds18b20_count = g_temps.deviceCount();
  g_status.ds18b20_mapping_explicit = g_temps.mappingExplicit();
  if (g_status.ds18b20_count > 0) {
    printOneWireRoms();
    g_temps.requestAll();
  }
#endif

#if ENABLE_DHT22
  g_status.dht_available = g_dht.begin(DHT22_PIN, DHT_TYPE);
#endif

#if ENABLE_SD
  g_status.sd_available = g_sd.begin(SD_CS_PIN, SD_LOG_PATH);
#endif

#if ENABLE_RAIN_GAUGE
  g_status.rain_available = g_rain.begin(RAIN_GAUGE_PIN, RAIN_MM_PER_TIP);
#endif

  g_status.simulation = SIMULATION_MODE;
}

static void initNetwork() {
  Serial.print("Connecting to WiFi");
  bool connected = g_wifi.begin(WIFI_SSID, WIFI_PASSWORD,
                                WIFI_CONNECT_TIMEOUT_MS);
  g_status.wifi_configured = g_wifi.configured();
  g_status.wifi_connected = connected;
  strncpy(g_status.ip, g_wifi.ip(), sizeof(g_status.ip) - 1);
  g_status.rssi = g_wifi.rssi();

  if (connected) {
    Serial.printf("WiFi connected. IP address: %s (%d dBm)\n", g_wifi.ip(),
                  g_wifi.rssi());
#if ENABLE_NTP
    if (g_wifi.syncTime(NTP_SERVER, TIMEZONE_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
                        5000)) {
      g_rtc.markNtpSynced();
      Serial.println("Clock synchronised over NTP.");
      if (g_rtc.available() && g_rtc.lostPower() && g_rtc.syncRtcFromSystem()) {
        g_status.rtc_lost_power = false;
        Serial.println("DS3231 set from NTP time.");
      }
    } else {
      Serial.println("WARNING: NTP sync failed - timestamps will be uptime only.");
    }
#endif
  } else if (!g_wifi.configured()) {
    Serial.println("WiFi not configured. Sensor acquisition continues.");
  } else {
    Serial.println("WARNING: WiFi connect failed. Sensor acquisition continues,");
    Serial.println("         records are buffered and retried in the background.");
  }

  g_backend.begin(BACKEND_URL, DEVICE_ID, BACKEND_API_KEY, FIRMWARE_VERSION,
                  HTTP_TIMEOUT_MS);
  g_status.backend_configured = g_backend.configured();

  TimeSource src;
  g_rtc.now(src);
  g_status.time_source = src;
}

static void acquire() {
  SensorData d;
  d.seq = ++g_seq;
  d.uptime_ms = millis();
  d.timestamp = g_rtc.now(d.time_source);

  d.light_status = g_light.read(d.light_lux);

  g_panelA.read(d.panelA);
  g_panelB.read(d.panelB);
  g_panelC.read(d.panelC);

#if ENABLE_DS18B20
  if (g_temps.available()) {
    d.panelA.temperature_status = g_temps.read(0, d.panelA.temperature);
    d.panelB.temperature_status = g_temps.read(1, d.panelB.temperature);
    d.panelC.temperature_status = g_temps.read(2, d.panelC.temperature);
    g_temps.requestAll();
  }
#endif

#if ENABLE_DHT22
  d.ambient_status = g_dht.read(d.ambient_temperature, d.humidity);
#endif

#if ENABLE_RAIN_GAUGE
  d.rain_status = g_rain.read(d.rainfall, d.rainfall_total);
#endif

#if SIMULATION_MODE
  Simulation::fill(d);
#endif

  g_processor.process(d);
  g_latest = d;

  g_status.bh1750 = d.light_status;
  g_status.ina_a = d.panelA.status;
  g_status.ina_b = d.panelB.status;
  g_status.ina_c = d.panelC.status;
  g_status.time_source = d.time_source;
  g_status.buffered = g_pending.size();
  g_status.buffer_dropped = g_pending.dropped();
  g_status.free_heap = ESP.getFreeHeap();
}

static void logRecord(const SensorData& d) {
  if (d.seq == 0) return;

  if (g_sd.available()) {
    if (g_sd.logRecord(d)) {
      g_status.sd_rows = g_sd.rowsWritten();
      return;
    }
    Serial.println("WARNING: SD write failed - falling back to Serial CSV.");
  }

#if SERIAL_CSV_ECHO
  static char row[512];
  if (SDLogger::buildCsvRow(d, row, sizeof(row)) > 0) {
    Serial.println(row);
  }
#endif
}

static void attemptUpload() {
  if (g_latest.seq == 0) return;

  if (!g_backend.configured()) {
    g_status.last_upload = BackendResult::NotConfigured;
    return;
  }

#if SIMULATION_MODE && !SIMULATION_ALLOW_UPLOAD
  if (g_latest.simulated) {
    g_status.last_upload = BackendResult::Suppressed;
    return;
  }
#endif

  BackendResult result = g_backend.send(g_latest);
  g_status.last_upload = result;
  g_status.last_http_code = g_backend.lastHttpCode();

  if (result == BackendResult::Sent) {
    g_status.uploads_ok++;
    drainBacklog();
  } else {
    g_status.uploads_failed++;
    g_pending.push(g_latest);
    Serial.printf("Backend upload failed (%s, http=%d)\n",
                  backendResultName(result), g_backend.lastHttpCode());
    Serial.printf("Saving data locally... (%u buffered, %s)\n",
                  (unsigned)g_pending.size(),
                  g_sd.available() ? "SD log active" : "Serial CSV only");
  }

  g_status.buffered = g_pending.size();
  g_status.buffer_dropped = g_pending.dropped();
}

static void drainBacklog() {
  for (int i = 0; i < UPLOAD_BACKLOG_PER_CYCLE && !g_pending.empty(); i++) {
    SensorData old;
    if (!g_pending.peek(old)) break;
    if (g_backend.send(old) != BackendResult::Sent) break;
    g_pending.pop();
    g_status.uploads_ok++;
  }
}

static void printJson(const SensorData& d) {
  static char json[1400];
  size_t n = g_backend.buildJson(d, json, sizeof(json));
  Serial.println();
  if (n == 0) {
    Serial.println("ERROR: payload did not fit the buffer.");
    return;
  }
  Serial.printf("POST %s  (%u bytes)\n",
                g_backend.configured() ? g_backend.url() : "<not configured>",
                (unsigned)n);
  Serial.println(json);
  Serial.println();
}

static void printCsv(const SensorData& d) {
  static char row[512];
  Serial.println();
  Serial.println(SDLogger::csvHeader());
  if (SDLogger::buildCsvRow(d, row, sizeof(row)) > 0) {
    Serial.println(row);
  }
  Serial.println();
}

static void printOneWireRoms() {
  char rom[24];
  Serial.printf("DS18B20 probes found on GPIO%d:\n", DS18B20_PIN);
  for (uint8_t i = 0; i < g_temps.deviceCount(); i++) {
    if (g_temps.romString(i, rom, sizeof(rom))) {
      Serial.printf("  [%u] %s\n", i, rom);
    }
  }
  if (!g_temps.mappingExplicit()) {
    Serial.println("  WARNING: DS18B20_ROM_PANEL_* not set in config.h.");
    Serial.println("  Discovery order is arbitrary, so panels may be swapped.");
    Serial.println("  Warm one probe by hand, see which index moves, then");
    Serial.println("  paste its ROM into the matching entry in config.h.");
  }
  Serial.println();
}

static void handleSerialCommand(char c) {
  switch (c) {
    case 'h':
      Dashboard::printHelp(Serial);
      break;
    case 'i':
      I2CScanner::scan(Wire, "Wire  (SDA 21 / SCL 22)", Serial);
      I2CScanner::scan(Wire1, "Wire1 (SDA 16 / SCL 17)", Serial);
      break;
    case 'r':
      acquire();
      Dashboard::print(Serial, g_latest, g_status, g_demo_view);
      break;
    case 'j':
      printJson(g_latest);
      break;
    case 'c':
      printCsv(g_latest);
      break;
    case 'u':
      Serial.println("Forcing backend upload...");
      attemptUpload();
      Serial.printf("Result: %s (http=%d)\n",
                    backendResultName(g_status.last_upload),
                    g_status.last_http_code);
      break;
    case 'd':
      g_demo_view = !g_demo_view;
      Serial.printf("View: %s\n", g_demo_view ? "DEMO" : "FULL DIAGNOSTIC");
      Dashboard::print(Serial, g_latest, g_status, g_demo_view);
      break;
    case 't':
      if (g_temps.available()) {
        printOneWireRoms();
      } else {
        Serial.println("No DS18B20 probes on the OneWire bus.");
      }
      break;
    case 's':
      Dashboard::printInitReport(Serial, g_status);
      break;
    default:
      break;
  }
}
