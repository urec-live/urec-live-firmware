// Gateway settings: which beacons exist, where data goes, and the timing rules.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gym_protocol.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "src/gateway/secrets.h not found: building with secrets.example.h (serial-only). Copy it to secrets.h."
#include "secrets.example.h"
#endif

#define GATEWAY_ID "gw-01"  // node name under /gym/gateways in Firebase

// Paper: "we stored each beacon identifier in the gateway beforehand". Each beacon is identified by
// (equipment type in its UUID, machine instance in the low byte of major); `code` is the machine's
// code in the UREC Live backend (seeded codes, see DataInitializer.java) and its Firebase key.
struct MachineConfig {
  uint16_t equipmentType;
  uint8_t instance;
  const char* code;
  const char* name;
};

static const MachineConfig kMachines[] = {
    {gym::kEquipLegCurl, 1, "LC001", "Leg Curl Machine 1"},
    {gym::kEquipLegExtension, 1, "LE001", "Leg Extension Machine 1"},
    {gym::kEquipLatPulldown, 1, "LP001", "Lat Pulldown 1"},
    {gym::kEquipSeatedRow, 1, "SR001", "Seated Row 1"},
};
static const size_t kMachineCount = sizeof(kMachines) / sizeof(kMachines[0]);

// ---- Set / occupancy rules (gym::MachineTracker) ----------------------------------------------
#define GATEWAY_SET_GAP_MS 8000        // no packet for this long: set is finished (beacon resets at 5 s)
#define GATEWAY_MACHINE_IDLE_MS 300000 // no packet for 5 min: machine is free again
#define GATEWAY_MIN_REPS_FOR_USE 2     // 1-rep "sets" are bumps (e.g. moving the selector pin)

// ---- BLE scanning -----------------------------------------------------------------------------
#define GATEWAY_SCAN_INTERVAL_MS 100  // interval == window: scan continuously (Espressif coex advice)
#define GATEWAY_SCAN_WINDOW_MS 100
#define GATEWAY_PACKET_QUEUE_LEN 64

// ---- Uploads ----------------------------------------------------------------------------------
#define GATEWAY_FIREBASE_ROOT "/gym"
#define GATEWAY_FIREBASE_MIN_INTERVAL_MS 500  // at most two live PATCHes per second
#define GATEWAY_HEARTBEAT_MS 60000
#define GATEWAY_UREC_CLAIM_ATTEMPTS 3  // "In Use" is best effort: give up after 3 tries
#define GATEWAY_UREC_RETRY_MS 10000    // first retry; doubles each time...
#define GATEWAY_UREC_MAX_RETRY_MS 300000  // ...up to 5 min. Releases are retried until they succeed.
#define GATEWAY_TLS_INSECURE 0  // 1 = skip certificate checks (bench only, never on shared Wi-Fi)
