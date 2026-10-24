// Shared HTTP/TLS settings for the gateway's clients.
#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "gateway_config.h"

inline void configureTls(WiFiClientSecure& tls) {
#if GATEWAY_TLS_INSECURE
  tls.setInsecure();
#else
  tls.useBuiltinCACertBundle();  // Mozilla CA list built into the core: Google + Let's Encrypt etc.
#endif
  tls.setHandshakeTimeout(10);  // seconds (default 120 would stall the loop for minutes)
}

inline void configureHttp(HTTPClient& http) {
  http.setConnectTimeout(5000);  // ms
  http.setTimeout(5000);         // ms
}
