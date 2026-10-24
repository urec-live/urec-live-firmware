#include "wifi_link.h"

#include <WiFi.h>
#include <string.h>

#include "gateway_config.h"

namespace {

const uint32_t kRetryMs = 15000;
bool g_wasConnected = false;
bool g_justConnected = false;
uint32_t g_lastAttemptMs = 0;

void connect() {
  if (strlen(WIFI_EAP_USERNAME) > 0) {
    const char* identity = strlen(WIFI_EAP_IDENTITY) > 0 ? WIFI_EAP_IDENTITY : WIFI_EAP_USERNAME;
    WiFi.begin(WIFI_SSID, WPA2_AUTH_PEAP, identity, WIFI_EAP_USERNAME, WIFI_EAP_PASSWORD);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  g_lastAttemptMs = millis();
}

}  // namespace

namespace wifi_link {

bool enabled() { return strlen(WIFI_SSID) > 0; }

void begin() {
  if (!enabled()) {
    Serial.println("[wifi] no SSID in secrets.h - serial-only mode");
    return;
  }
  WiFi.mode(WIFI_STA);
  // Keep the default modem sleep: with BLE running, Wi-Fi and BLE share the radio by time slices
  // and WiFi.setSleep(false) is not allowed.
  WiFi.setAutoReconnect(true);
  Serial.printf("[wifi] connecting to \"%s\"\n", WIFI_SSID);
  connect();
}

void update(uint32_t nowMs) {
  if (!enabled()) return;
  const bool up = WiFi.status() == WL_CONNECTED;
  if (up && !g_wasConnected) {
    Serial.printf("[wifi] connected, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
    g_justConnected = true;
  } else if (!up && g_wasConnected) {
    Serial.println("[wifi] disconnected");
  }
  g_wasConnected = up;
  if (!up && nowMs - g_lastAttemptMs >= kRetryMs) {
    Serial.println("[wifi] retrying");
    WiFi.disconnect();
    connect();
  }
}

bool connected() { return enabled() && WiFi.status() == WL_CONNECTED; }

bool takeJustConnected() {
  const bool j = g_justConnected;
  g_justConnected = false;
  return j;
}

}  // namespace wifi_link
