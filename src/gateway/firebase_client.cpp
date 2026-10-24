#include "firebase_client.h"

#include <ArduinoJson.h>
#include <string.h>

#include "gateway_config.h"
#include "net_util.h"

namespace {
const int kAuthUnavailable = -1000;
const uint32_t kSignInRetryMs = 30000;
const uint32_t kRenewBeforeExpiryS = 600;  // renew 10 min before the 1 h token expires
}  // namespace

bool FirebaseClient::configured() const { return strlen(FIREBASE_DB_URL) > 0; }

bool FirebaseClient::authEnabled() const { return strlen(FIREBASE_EMAIL) > 0; }

void FirebaseClient::begin() {
  configureTls(tls_);
  configureHttp(http_);
  http_.setReuse(true);
}

bool FirebaseClient::ensureToken(bool force) {
  if (!authEnabled()) return true;
  const uint32_t now = millis();
  const bool fresh = token_.length() > 0 && now - tokenIssuedMs_ < tokenUsableMs_;
  if (fresh && !force) return true;
  // At most one sign-in per kSignInRetryMs, whatever asks for it.
  if (signInAttempted_ && now - lastSignInMs_ < kSignInRetryMs) return fresh;
  signInAttempted_ = true;
  lastSignInMs_ = now;

  // Short-lived client: the auth host differs from the database host, and HTTPClient drops a
  // kept-alive connection whenever the host changes.
  WiFiClientSecure tls;
  configureTls(tls);
  HTTPClient http;
  configureHttp(http);
  const String url = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") +
                     FIREBASE_API_KEY;
  if (!http.begin(tls, url)) return false;
  http.addHeader("Content-Type", "application/json");

  JsonDocument req;
  req["email"] = FIREBASE_EMAIL;
  req["password"] = FIREBASE_PASSWORD;
  req["returnSecureToken"] = true;
  String body;
  serializeJson(req, body);

  const int code = http.POST(body);
  if (code != 200) {
    Serial.printf("[firebase] sign-in failed: HTTP %d %s\n", code,
                  code > 0 ? http.getString().c_str() : http.errorToString(code).c_str());
    http.end();
    return false;
  }
  JsonDocument filter;
  filter["idToken"] = true;
  filter["expiresIn"] = true;
  JsonDocument resp;
  const DeserializationError err = deserializeJson(resp, http.getString(), DeserializationOption::Filter(filter));
  http.end();
  if (err || !resp["idToken"].is<const char*>()) {
    Serial.println("[firebase] sign-in: unexpected response");
    return false;
  }
  token_ = resp["idToken"].as<String>();
  long expiresIn = resp["expiresIn"].as<String>().toInt();  // a string, e.g. "3600"
  if (expiresIn <= static_cast<long>(kRenewBeforeExpiryS)) expiresIn = kRenewBeforeExpiryS + 60;
  tokenIssuedMs_ = millis();
  tokenUsableMs_ = static_cast<uint32_t>(expiresIn - kRenewBeforeExpiryS) * 1000u;
  Serial.printf("[firebase] signed in as %s\n", FIREBASE_EMAIL);
  return true;
}

int FirebaseClient::send(const char* method, const String& path, const String& json) {
  if (!configured()) return kAuthUnavailable;
  if (!ensureToken(false)) return kAuthUnavailable;

  int code = 0;
  for (int attempt = 0; attempt < 2; attempt++) {
    String url = String(FIREBASE_DB_URL) + path + ".json?print=silent";
    if (token_.length() > 0) url += "&auth=" + token_;
    if (!http_.begin(tls_, url)) {
      code = HTTPC_ERROR_CONNECTION_REFUSED;
    } else {
      http_.addHeader("Content-Type", "application/json");
      code = http_.sendRequest(method, json);
      if (code >= 400) Serial.printf("[firebase] %s %s -> %d %s\n", method, path.c_str(), code, http_.getString().c_str());
      http_.end();  // keeps the TLS connection open when the server allows it
    }
    // 401: expired token, or rules that deny the write. A token younger than the sign-in
    // throttle cannot have expired, so that 401 is the rules: report it instead of renewing.
    if (code == 401 && attempt == 0 && authEnabled() && millis() - tokenIssuedMs_ >= kSignInRetryMs) {
      if (!ensureToken(true)) break;
      continue;
    }
    if (code < 0 && attempt == 0) {  // stale kept-alive socket: reconnect once
      tls_.stop();
      continue;
    }
    break;
  }
  return code;
}
