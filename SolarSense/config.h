#pragma once

#include <Arduino.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef SS_WIFI_SSID
#define SS_WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef SS_WIFI_PASSWORD
#define SS_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif
#ifndef SS_BACKEND_URL
#define SS_BACKEND_URL ""
#endif
#ifndef SS_BACKEND_API_KEY
#define SS_BACKEND_API_KEY ""
#endif

static const char* const FIRMWARE_VERSION = "0.5.0";
static const char* const DEVICE_ID        = "SOLARSENSE_01";

static const char* const WIFI_SSID     = SS_WIFI_SSID;
static const char* const WIFI_PASSWORD = SS_WIFI_PASSWORD;
static const char* const BACKEND_URL   = SS_BACKEND_URL;
static const char* const BACKEND_API_KEY = SS_BACKEND_API_KEY;

#define DEMO_MODE false

#define SIMULATION_MODE false
#define SIMULATION_ALLOW_UPLOAD false

static const unsigned long SENSOR_INTERVAL_MS    = 5000UL;
static const unsigned long DASHBOARD_INTERVAL_MS = 5000UL;
static const unsigned long SD_LOG_INTERVAL_MS    = 5000UL;
static const unsigned long UPLOAD_INTERVAL_MS    = 30000UL;
static const unsigned long WIFI_CHECK_INTERVAL_MS = 10000UL;

static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000UL;
static const unsigned long HTTP_TIMEOUT_MS = 4000UL;

#define I2C0_SDA_PIN 21
#define I2C0_SCL_PIN 22
#define I2C1_SDA_PIN 16
#define I2C1_SCL_PIN 17
static const uint32_t I2C_CLOCK_HZ = 400000UL;

#define BUS_WIRE  0
#define BUS_WIRE1 1
#define INA219_ADDRESSES_SOLDERED false

#if INA219_ADDRESSES_SOLDERED
  #define PANEL_A_ENABLED true
  #define PANEL_A_BUS     BUS_WIRE
  #define PANEL_A_ADDRESS 0x40

  #define PANEL_B_ENABLED true
  #define PANEL_B_BUS     BUS_WIRE
  #define PANEL_B_ADDRESS 0x41

  #define PANEL_C_ENABLED true
  #define PANEL_C_BUS     BUS_WIRE
  #define PANEL_C_ADDRESS 0x44
#else
  #define PANEL_A_ENABLED true
  #define PANEL_A_BUS     BUS_WIRE
  #define PANEL_A_ADDRESS 0x40

  #define PANEL_B_ENABLED true
  #define PANEL_B_BUS     BUS_WIRE1
  #define PANEL_B_ADDRESS 0x40

  #define PANEL_C_ENABLED false
  #define PANEL_C_BUS     BUS_WIRE1
  #define PANEL_C_ADDRESS 0x41
#endif

#define INA219_RANGE INA219Panel::RANGE_32V_2A

#define BH1750_I2C_ADDRESS 0x23
#define BH1750_MTREG 31

#define ENABLE_RTC      true
#define ENABLE_DS18B20  true
#define ENABLE_DHT22    true
#define ENABLE_SD       true
#define ENABLE_RAIN_GAUGE false

#define DS18B20_PIN 4
#define DHT22_PIN   15
#define DHT_TYPE    DHT22

#define SD_CS_PIN   5
static const char* const SD_LOG_PATH = "/solarsense.csv";

#define RAIN_GAUGE_PIN 27
static const float RAIN_MM_PER_TIP = 0.2794f;

static const uint8_t DS18B20_ROM_PANEL_A[8] = {0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t DS18B20_ROM_PANEL_B[8] = {0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t DS18B20_ROM_PANEL_C[8] = {0, 0, 0, 0, 0, 0, 0, 0};

#define DS18B20_RESOLUTION 11

#define ENABLE_NTP true
static const char* const NTP_SERVER = "pool.ntp.org";
static const long  TIMEZONE_OFFSET_SEC = 19800;
static const int   DAYLIGHT_OFFSET_SEC = 0;

static const float MIN_LUX_FOR_COMPARISON = 20000.0f;
static const float MIN_REFERENCE_POWER_W = 0.5f;

#define SERIAL_CSV_ECHO true
#define UPLOAD_BUFFER_SIZE 60
#define UPLOAD_BACKLOG_PER_CYCLE 5

#define SERIAL_BAUD 115200
