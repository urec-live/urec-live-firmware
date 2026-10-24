// Firebase Realtime Database over its REST API (HTTPS + JSON), as in the paper's gateway
// ("an updated HTTP PATCH packet to the Google database ... JSON-form body").
//
// Auth: if FIREBASE_EMAIL is set the gateway signs in as that Firebase user and appends the ID
// token as ?auth=; tokens last one hour and are renewed early or after a 401.
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

class FirebaseClient {
 public:
  bool configured() const;
  void begin();

  // Write `json` at `path` (e.g. "/gym/machines"; ".json" is appended). Returns the HTTP status
  // (204 on success with print=silent) or a negative HTTPClient error.
  int patch(const String& path, const String& json) { return send("PATCH", path, json); }
  int put(const String& path, const String& json) { return send("PUT", path, json); }

 private:
  int send(const char* method, const String& path, const String& json);
  bool authEnabled() const;
  bool ensureToken(bool force);

  WiFiClientSecure tls_;  // kept open between requests to the database host (keep-alive)
  HTTPClient http_;
  String token_;
  // Elapsed-time bookkeeping (now - start >= duration), safe across millis() wrap-around.
  uint32_t tokenIssuedMs_ = 0;
  uint32_t tokenUsableMs_ = 0;  // renew once the token is this old
  uint32_t lastSignInMs_ = 0;
  bool signInAttempted_ = false;
};
