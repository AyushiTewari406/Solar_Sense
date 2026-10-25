#pragma once

#include <Arduino.h>

#include "../data/SensorData.h"

enum class BackendResult : uint8_t {
  Sent = 0,
  NotConfigured,
  NoNetwork,
  HttpError,
  PayloadTooLarge,
  Suppressed
};

const char* backendResultName(BackendResult r);

class BackendClient {
 public:
  void begin(const char* url, const char* device_id, const char* api_key,
             const char* firmware_version, unsigned long timeout_ms);

  bool configured() const { return configured_; }
  const char* url() const { return url_; }

  size_t buildJson(const SensorData& d, char* out, size_t cap) const;

  BackendResult send(const SensorData& d);

  int lastHttpCode() const { return last_http_code_; }
  BackendResult lastResult() const { return last_result_; }

 private:
  const char* url_ = "";
  const char* device_id_ = "SOLARSENSE";
  const char* api_key_ = "";
  const char* firmware_version_ = "0.0.0";
  unsigned long timeout_ms_ = 4000;
  bool configured_ = false;
  int last_http_code_ = 0;
  BackendResult last_result_ = BackendResult::NotConfigured;
};
