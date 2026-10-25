#include "BackendClient.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <math.h>
#include <stdarg.h>

const char* backendResultName(BackendResult r) {
  switch (r) {
    case BackendResult::Sent:            return "SENT";
    case BackendResult::NoNetwork:       return "NO NETWORK";
    case BackendResult::HttpError:       return "HTTP ERROR";
    case BackendResult::PayloadTooLarge: return "PAYLOAD TOO LARGE";
    case BackendResult::Suppressed:      return "SUPPRESSED (simulated data)";
    case BackendResult::NotConfigured:
    default:                             return "NOT CONFIGURED";
  }
}

namespace {

struct Appender {
  char* buf;
  size_t cap;
  size_t len = 0;
  bool overflow = false;

  Appender(char* b, size_t c) : buf(b), cap(c) {
    if (cap > 0) buf[0] = '\0';
  }

  void addf(const char* fmt, ...) {
    if (overflow || cap == 0 || len + 1 >= cap) {
      overflow = true;
      return;
    }
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(buf + len, cap - len, fmt, ap);
    va_end(ap);
    if (written < 0 || (size_t)written >= cap - len) {
      overflow = true;
      return;
    }
    len += (size_t)written;
  }
};

void addNumber(Appender& a, const char* key, float value, int decimals) {
  if (isnan(value)) {
    a.addf("\"%s\":null", key);
  } else {
    a.addf("\"%s\":%.*f", key, decimals, value);
  }
}

void addPanel(Appender& a, const char* key, const PanelReading& p) {
  a.addf("\"%s\":{", key);
  addNumber(a, "voltage", p.voltage, 2);
  a.addf(",");
  addNumber(a, "current", p.current, 3);
  a.addf(",");
  addNumber(a, "power", p.power, 2);
  a.addf(",");
  addNumber(a, "temperature", p.temperature, 2);
  a.addf(",\"status\":\"%s\"", sensorStatusName(p.status));
  a.addf(",\"temperature_status\":\"%s\"",
         sensorStatusName(p.temperature_status));
  a.addf("}");
}

}

void BackendClient::begin(const char* url, const char* device_id,
                          const char* api_key, const char* firmware_version,
                          unsigned long timeout_ms) {
  url_ = url != nullptr ? url : "";
  device_id_ = device_id;
  api_key_ = api_key != nullptr ? api_key : "";
  firmware_version_ = firmware_version;
  timeout_ms_ = timeout_ms;
  configured_ = strlen(url_) > 0;
  last_result_ = configured_ ? BackendResult::NoNetwork
                             : BackendResult::NotConfigured;
}

size_t BackendClient::buildJson(const SensorData& d, char* out,
                                size_t cap) const {
  char ts[32];
  formatTimestampIso(d, ts, sizeof(ts));

  Appender a(out, cap);
  a.addf("{");
  a.addf("\"device_id\":\"%s\"", device_id_);
  a.addf(",\"seq\":%lu", (unsigned long)d.seq);
  a.addf(",\"timestamp\":\"%s\"", ts);
  a.addf(",\"time_source\":\"%s\"", timeSourceName(d.time_source));
  a.addf(",\"uptime_ms\":%lu", d.uptime_ms);
  a.addf(",");
  addNumber(a, "light_lux", d.light_lux, 1);
  a.addf(",\"light_status\":\"%s\"", sensorStatusName(d.light_status));
  a.addf(",");
  addPanel(a, "panelA", d.panelA);
  a.addf(",");
  addPanel(a, "panelB", d.panelB);
  a.addf(",");
  addPanel(a, "panelC", d.panelC);

  a.addf(",\"ambient\":{");
  addNumber(a, "temperature", d.ambient_temperature, 2);
  a.addf(",");
  addNumber(a, "humidity", d.humidity, 1);
  a.addf(",");
  addNumber(a, "rainfall_mm", d.rainfall, 3);
  a.addf(",");
  addNumber(a, "rainfall_total_mm", d.rainfall_total, 3);
  a.addf(",\"status\":\"%s\"", sensorStatusName(d.ambient_status));
  a.addf("}");

  a.addf(",\"comparison\":{\"valid\":%s", d.comparison_valid ? "true" : "false");
  a.addf(",\"note\":\"%s\"", d.comparison_note);
  a.addf(",");
  addNumber(a, "efficiency_B", d.efficiency_B, 4);
  a.addf(",");
  addNumber(a, "efficiency_C", d.efficiency_C, 4);
  a.addf(",");
  addNumber(a, "loss_B_pct", d.loss_B_pct, 2);
  a.addf(",");
  addNumber(a, "loss_C_pct", d.loss_C_pct, 2);
  a.addf("}");

  a.addf(",\"firmware\":\"%s\"", firmware_version_);
  a.addf(",\"simulated\":%s", d.simulated ? "true" : "false");
  a.addf("}");

  return a.overflow ? 0 : a.len;
}

BackendResult BackendClient::send(const SensorData& d) {
  if (!configured_) {
    last_result_ = BackendResult::NotConfigured;
    return last_result_;
  }
  if (WiFi.status() != WL_CONNECTED) {
    last_result_ = BackendResult::NoNetwork;
    return last_result_;
  }

  static char payload[1400];
  size_t len = buildJson(d, payload, sizeof(payload));
  if (len == 0) {
    last_result_ = BackendResult::PayloadTooLarge;
    return last_result_;
  }

  HTTPClient http;
  http.setConnectTimeout((int)timeout_ms_);
  http.setTimeout((uint16_t)timeout_ms_);
  http.setReuse(false);

  if (!http.begin(url_)) {
    last_http_code_ = 0;
    last_result_ = BackendResult::HttpError;
    return last_result_;
  }
  http.addHeader("Content-Type", "application/json");
  if (strlen(api_key_) > 0) {
    http.addHeader("X-API-Key", api_key_);
  }

  last_http_code_ = http.POST((uint8_t*)payload, len);
  http.end();

  last_result_ = (last_http_code_ >= 200 && last_http_code_ < 300)
                     ? BackendResult::Sent
                     : BackendResult::HttpError;
  return last_result_;
}
