#include "urec_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

#include "gateway_config.h"
#include "net_util.h"

namespace {

bool isHttps() { return strncmp(UREC_BASE_URL, "https://", 8) == 0; }

// Runs one request on a fresh connection: status changes are minutes apart and Railway closes
// idle connections after 60 s, so keep-alive would not help.
int request(const char* method, const String& url, const String& body, String* response) {
  WiFiClientSecure tls;
  WiFiClient plain;
  HTTPClient http;
  configureHttp(http);
  http.setReuse(false);
  bool ok;
  if (isHttps()) {
    configureTls(tls);
    ok = http.begin(tls, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) return HTTPC_ERROR_CONNECTION_REFUSED;
  if (body.length() > 0) http.addHeader("Content-Type", "application/json");
  const int code = http.sendRequest(method, body);
  if (response != nullptr && code > 0) *response = http.getString();
  http.end();
  return code;
}

}  // namespace

bool UrecClient::configured() const { return strlen(UREC_BASE_URL) > 0; }

int UrecClient::getStatus(const char* code, String* status, String* name) {
  String response;
  const int http = request("GET", String(UREC_BASE_URL) + "/api/machines/code/" + code, "", &response);
  if (http != 200) return http;
  JsonDocument filter;
  filter["status"] = true;
  filter["name"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, response, DeserializationOption::Filter(filter))) return HTTPC_ERROR_NO_HTTP_SERVER;
  if (status != nullptr) *status = doc["status"] | "";
  if (name != nullptr) *name = doc["name"] | "";
  return http;
}

int UrecClient::putStatus(const char* code, const char* status) {
  JsonDocument body;
  body["status"] = status;
  String json;
  serializeJson(body, json);
  return request("PUT", String(UREC_BASE_URL) + "/api/machines/code/" + code + "/status", json, nullptr);
}
