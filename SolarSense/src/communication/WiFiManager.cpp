#include "WiFiManager.h"

#include <WiFi.h>
#include <time.h>

static const unsigned long kReconnectBackoffMs = 15000;
static const time_t kPlausibleEpoch = 1672531200;

bool WiFiLink::begin(const char* ssid, const char* password,
                     unsigned long timeout_ms) {
  ssid_ = ssid;
  password_ = password;
  configured_ = ssid != nullptr && strlen(ssid) > 0 &&
                strcmp(ssid, "YOUR_WIFI_SSID") != 0;
  if (!configured_) return false;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid_, password_);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    strncpy(ip_, WiFi.localIP().toString().c_str(), sizeof(ip_) - 1);
    ip_[sizeof(ip_) - 1] = '\0';
    return true;
  }
  return false;
}

bool WiFiLink::connected() const {
  return WiFi.status() == WL_CONNECTED;
}

int WiFiLink::rssi() const {
  return connected() ? WiFi.RSSI() : 0;
}

void WiFiLink::loop() {
  if (!configured_) return;

  if (connected()) {
    strncpy(ip_, WiFi.localIP().toString().c_str(), sizeof(ip_) - 1);
    ip_[sizeof(ip_) - 1] = '\0';
    return;
  }

  strncpy(ip_, "0.0.0.0", sizeof(ip_));
  unsigned long now = millis();
  if (now - last_reconnect_attempt_ms_ < kReconnectBackoffMs) return;
  last_reconnect_attempt_ms_ = now;

  WiFi.disconnect();
  WiFi.begin(ssid_, password_);
}

bool WiFiLink::syncTime(const char* ntp_server, long tz_offset_sec,
                        int dst_offset_sec, unsigned long timeout_ms) {
  if (!connected()) return false;

  configTime(tz_offset_sec, dst_offset_sec, ntp_server);

  unsigned long start = millis();
  while ((millis() - start) < timeout_ms) {
    if (time(nullptr) > kPlausibleEpoch) {
      time_synced_ = true;
      return true;
    }
    delay(200);
  }
  return false;
}
