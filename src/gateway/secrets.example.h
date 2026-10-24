// Template for src/gateway/secrets.h (git-ignored). Copy this file to secrets.h and fill it in.
// An empty string switches the feature off, so the gateway also runs with nothing filled in:
// it then only scans and prints the repetition vector on the serial monitor.
#pragma once

// ---- Wi-Fi (2.4 GHz only) ---------------------------------------------------------------------
// Campus networks are often WPA2-Enterprise or captive-portal; a phone hotspot is easiest
// (iPhone: Settings > Personal Hotspot > Maximize Compatibility, to force 2.4 GHz).
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
// WPA2-Enterprise (PEAP, e.g. eduroam): fill in the username to use it instead of WIFI_PASSWORD.
#define WIFI_EAP_IDENTITY ""  // outer identity; leave empty to reuse the username
#define WIFI_EAP_USERNAME ""
#define WIFI_EAP_PASSWORD ""

// ---- Firebase Realtime Database ---------------------------------------------------------------
// Database URL from the Realtime Database page, without a trailing slash, e.g.
// "https://getmefit-default-rtdb.firebaseio.com" or
// "https://getmefit-default-rtdb.europe-west1.firebasedatabase.app"
#define FIREBASE_DB_URL ""
// Gateway login (Authentication > Sign-in method > Email/Password, then add a user).
// Leave FIREBASE_EMAIL empty only while the database is in test mode (open rules).
#define FIREBASE_API_KEY ""  // Project settings > General > Web API key
#define FIREBASE_EMAIL ""
#define FIREBASE_PASSWORD ""

// ---- UREC Live backend ------------------------------------------------------------------------
// Every status change is logged as a CHECK_IN/CHECK_OUT in the backend's analytics, so test
// against a local backend first, e.g. "http://192.168.1.50:8080" (no trailing slash).
// Production: "https://urec-live-backend-production.up.railway.app"
#define UREC_BASE_URL ""
