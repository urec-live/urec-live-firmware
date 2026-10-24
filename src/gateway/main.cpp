// GetMeFit gateway: ESP32-C3-DevKitM-1 that listens to the beacons and forwards their reps.
//
// Reproduces the gateway of Bian, Rupp & Magno (arXiv 2305.17594): a whitelist of known beacons,
// a "repetition vector" with one slot per machine, and HTTP PATCHes of JSON to a Firebase
// database. On top of that it records finished sets and marks machines "In Use"/"Available" in
// the UREC Live backend. docs/rep-counting-walkthrough.md explains every step.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_random.h>

#include "ble_scanner.h"
#include "firebase_client.h"
#include "gateway_config.h"
#include "gym_protocol.h"
#include "machine_tracker.h"
#include "status_led.h"
#include "urec_client.h"
#include "wifi_link.h"

namespace {

const size_t kMaxPendingSets = 16;
const uint32_t kScanCheckMs = 5000;
const uint32_t kUploadRetryMs = 5000;

gym::TrackerConfig trackerConfig() {
  gym::TrackerConfig c;
  c.setGapMs = GATEWAY_SET_GAP_MS;
  c.idleMs = GATEWAY_MACHINE_IDLE_MS;
  c.minRepsForUse = GATEWAY_MIN_REPS_FOR_USE;
  return c;
}

// Timers are "start + duration" checked as now - start >= duration (unsigned), which stays
// correct across millis() wrap-around however long a timer sits unused.
bool elapsed(uint32_t now, uint32_t start, uint32_t duration) { return now - start >= duration; }
bool httpOk(int code) { return code >= 200 && code < 300; }

// True only when a request certainly did not take effect: it never got fully sent (HTTPClient
// fails these before the request reaches the socket), or the backend answered and refused it.
// Anything else - timeouts, lost connections, 5xx - may have been applied.
bool certainlyNotApplied(int code) {
  return code == HTTPC_ERROR_CONNECTION_REFUSED || code == HTTPC_ERROR_SEND_HEADER_FAILED ||
         code == HTTPC_ERROR_SEND_PAYLOAD_FAILED || (code >= 300 && code < 500);
}

gym::MachineTracker g_tracker(kMachineCount, trackerConfig());
FirebaseClient g_firebase;
UrecClient g_urec;
StatusLed g_led;
Preferences g_prefs;
char g_bootId[9] = "00000000";  // random per boot: makes set keys unique across reboots

// Firebase: machines whose live state still has to be uploaded, and finished sets.
bool g_fbLiveDirty[gym::MachineTracker::kMaxMachines] = {};
uint32_t g_lastLivePatchMs = 0;
uint32_t g_livePatchWaitMs = 0;  // 0 = upload as soon as something changes
gym::SetRecord g_pendingSets[kMaxPendingSets];
size_t g_pendingSetCount = 0;
uint32_t g_lastSetFailMs = 0;
uint32_t g_setRetryWaitMs = 0;  // 0 = no failed upload to back off from
uint32_t g_lastHeartbeatMs = 0;
bool g_heartbeatSent = false;

// UREC: the gateway only releases machines it marked "In Use" itself ("claimed").
struct UrecSync {
  bool claimed;
  bool wantInUse;
  bool pending;
  uint8_t attempts;
  uint32_t lastTryMs;
  uint32_t waitMs;  // try again once this long has passed since lastTryMs
};
UrecSync g_urecSync[gym::MachineTracker::kMaxMachines] = {};
bool g_bootChecksDone = false;

uint32_t g_unknownBeacons[8];  // (type << 8 | instance) already reported
size_t g_unknownCount = 0;
uint32_t g_lastScanCheckMs = 0;

int findMachine(uint16_t type, uint8_t instance) {
  for (size_t i = 0; i < kMachineCount; i++) {
    if (kMachines[i].equipmentType == type && kMachines[i].instance == instance) return static_cast<int>(i);
  }
  return -1;
}

void noteUnknownBeacon(const BeaconPacket& p) {
  const uint32_t key = (static_cast<uint32_t>(p.equipmentType) << 8) | p.machineInstance;
  for (size_t i = 0; i < g_unknownCount; i++) {
    if (g_unknownBeacons[i] == key) return;
  }
  if (g_unknownCount < sizeof(g_unknownBeacons) / sizeof(g_unknownBeacons[0])) g_unknownBeacons[g_unknownCount++] = key;
  Serial.printf("[ble] GetMeFit beacon %s #%u is not in kMachines (gateway_config.h) - ignored\n",
                gym::equipmentName(p.equipmentType), p.machineInstance);
}

// The paper's "repetition vector", one slot per configured machine.
void printVector() {
  Serial.print("[reps]");
  for (size_t m = 0; m < kMachineCount; m++) {
    const gym::MachineState& st = g_tracker.state(m);
    Serial.printf(" %s=%u", kMachines[m].code, st.liveReps());
    if (st.seen) Serial.printf(" (set %lu%s, %d dBm)", static_cast<unsigned long>(st.setNumber),
                               st.inUse ? ", in use" : "", st.rssi);
    if (m + 1 < kMachineCount) Serial.print(" |");
  }
  Serial.println();
}

void queueSet(const gym::SetRecord& rec) {
  Serial.printf("[set] %s set %lu: %u reps in %.1f s\n", kMachines[rec.machine].code,
                static_cast<unsigned long>(rec.setNumber), rec.reps, (rec.endMs - rec.startMs) / 1000.0f);
  if (!g_firebase.configured()) return;
  // An amended set (late packet) replaces its queued version instead of being uploaded twice.
  for (size_t i = 0; i < g_pendingSetCount; i++) {
    if (g_pendingSets[i].machine == rec.machine && g_pendingSets[i].setNumber == rec.setNumber) {
      g_pendingSets[i] = rec;
      return;
    }
  }
  if (g_pendingSetCount == kMaxPendingSets) {  // offline for a long time: drop the oldest
    for (size_t i = 1; i < kMaxPendingSets; i++) g_pendingSets[i - 1] = g_pendingSets[i];
    g_pendingSetCount--;
  }
  g_pendingSets[g_pendingSetCount++] = rec;
}

// ---------------------------------------------------------------- Firebase

void uploadLive(uint32_t now) {
  bool any = false;
  for (size_t m = 0; m < kMachineCount; m++) any = any || g_fbLiveDirty[m];
  if (!any || !elapsed(now, g_lastLivePatchMs, g_livePatchWaitMs)) return;
  g_lastLivePatchMs = now;

  // One multi-path PATCH updates every changed machine without touching the others.
  JsonDocument doc;
  for (size_t m = 0; m < kMachineCount; m++) {
    if (!g_fbLiveDirty[m]) continue;
    const gym::MachineState& st = g_tracker.state(m);
    const String k = String(kMachines[m].code) + "/";
    doc[k + "name"] = kMachines[m].name;
    doc[k + "type"] = gym::equipmentName(kMachines[m].equipmentType);
    doc[k + "reps"] = st.liveReps();
    doc[k + "setNumber"] = st.setNumber;
    doc[k + "inUse"] = st.inUse;
    doc[k + "lastSetReps"] = st.lastSetReps;
    doc[k + "rssi"] = st.rssi;
    doc[k + "updatedAt"][".sv"] = "timestamp";
  }
  String body;
  serializeJson(doc, body);
  const int code = g_firebase.patch(GATEWAY_FIREBASE_ROOT "/machines", body);
  if (httpOk(code)) {
    for (size_t m = 0; m < kMachineCount; m++) g_fbLiveDirty[m] = false;
    g_livePatchWaitMs = GATEWAY_FIREBASE_MIN_INTERVAL_MS;
    g_led.flash(0, 40, 0);
  } else {
    Serial.printf("[firebase] live update failed (%d), retrying\n", code);
    g_livePatchWaitMs = kUploadRetryMs;
    g_led.flash(40, 0, 0, 200);
  }
}

void uploadSets(uint32_t now) {
  if (!elapsed(now, g_lastSetFailMs, g_setRetryWaitMs)) return;
  while (g_pendingSetCount > 0) {
    const gym::SetRecord& rec = g_pendingSets[0];
    // Deterministic key: re-uploading the same (or an amended) set overwrites, never duplicates.
    const String path = String(GATEWAY_FIREBASE_ROOT "/sets/") + kMachines[rec.machine].code + "/" +
                        g_bootId + "_" + String(rec.setNumber);
    JsonDocument doc;
    doc["reps"] = rec.reps;
    doc["setSeq"] = rec.setSeq;
    doc["durationMs"] = rec.endMs - rec.startMs;
    doc["endedAt"][".sv"] = "timestamp";  // upload time; sets are uploaded within seconds
    doc["gateway"] = GATEWAY_ID;
    String body;
    serializeJson(doc, body);
    const int code = g_firebase.put(path, body);
    if (!httpOk(code)) {
      Serial.printf("[firebase] set upload failed (%d), retrying\n", code);
      g_lastSetFailMs = now;
      g_setRetryWaitMs = kUploadRetryMs;
      return;
    }
    g_setRetryWaitMs = 0;
    for (size_t i = 1; i < g_pendingSetCount; i++) g_pendingSets[i - 1] = g_pendingSets[i];
    g_pendingSetCount--;
  }
}

void uploadHeartbeat(uint32_t now) {
  if (g_heartbeatSent && !elapsed(now, g_lastHeartbeatMs, GATEWAY_HEARTBEAT_MS)) return;
  g_heartbeatSent = true;
  g_lastHeartbeatMs = now;
  JsonDocument doc;
  doc["lastSeen"][".sv"] = "timestamp";
  doc["bootId"] = g_bootId;
  doc["uptimeS"] = now / 1000;
  doc["ip"] = WiFi.localIP().toString();
  doc["wifiRssi"] = WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["maxAllocHeap"] = ESP.getMaxAllocHeap();
  doc["packets"] = ble_scanner::packetsSeen();
  doc["packetsDropped"] = ble_scanner::packetsDropped();
  doc["setsDropped"] = g_tracker.droppedSets();
  String body;
  serializeJson(doc, body);
  const int code = g_firebase.patch(GATEWAY_FIREBASE_ROOT "/gateways/" GATEWAY_ID, body);
  if (!httpOk(code)) Serial.printf("[firebase] heartbeat failed (%d)\n", code);
}

// ---------------------------------------------------------------- UREC Live

void saveClaim(size_t m, bool claimed) {
  g_urecSync[m].claimed = claimed;
  g_prefs.putBool(kMachines[m].code, claimed);  // survives a reboot so a claim is never orphaned
}

void onUseChanged(size_t m, uint32_t now) {
  UrecSync& u = g_urecSync[m];
  u.wantInUse = g_tracker.state(m).inUse;
  u.pending = true;
  u.attempts = 0;
  u.lastTryMs = now;
  u.waitMs = 0;
}

void syncUrecMachine(size_t m, uint32_t now) {
  UrecSync& u = g_urecSync[m];
  const char* code = kMachines[m].code;
  if (!u.wantInUse && !u.claimed) {  // someone else's check-in is none of our business
    u.pending = false;
    return;
  }
  String status;
  int http = g_urec.getStatus(code, &status);
  bool done = false;
  if (http == 200) {
    if (u.wantInUse) {
      if (status == "Available") {
        // Record the claim first: if the PUT succeeds but its response is lost, the retry sees
        // "In Use" and the machine is still released later instead of staying "In Use" forever.
        if (!u.claimed) saveClaim(m, true);
        http = g_urec.putStatus(code, "In Use");
        if (httpOk(http)) {
          Serial.printf("[urec] %s -> In Use\n", code);
          g_led.flash(0, 40, 0);
          done = true;
        } else if (http == 403 || http == 409) {  // Out of Order
          saveClaim(m, false);
          done = true;
        } else if (certainlyNotApplied(http)) {
          // Not applied: drop the claim, or a member's QR check-in made before the retry would
          // look like ours and be released later.
          saveClaim(m, false);
        }
      } else {
        // "In Use" (ours if claimed, else a QR check-in), "Out of Order", ...: leave it alone.
        done = true;
      }
    } else {
      if (status == "In Use") {
        http = g_urec.putStatus(code, "Available");
        if (httpOk(http) || (http >= 400 && http < 500)) {
          Serial.printf("[urec] %s -> Available (%d)\n", code, http);
          saveClaim(m, false);
          done = true;
        }
      } else {
        saveClaim(m, false);  // someone already changed it
        done = true;
      }
    }
  }

  if (done) {
    u.pending = false;
    return;
  }
  // Back off 10 s, 20 s, 40 s ... up to 5 min. A release never gives up: a machine we marked
  // "In Use" must not stay that way because the backend was briefly unreachable.
  if (u.attempts < 255) u.attempts++;
  g_led.flash(40, 0, 0, 200);
  if (u.wantInUse && u.attempts >= GATEWAY_UREC_CLAIM_ATTEMPTS) {
    // Marking "In Use" is best effort; an unknown machine code shows up in the boot check.
    Serial.printf("[urec] %s: HTTP %d while claiming, giving up\n", code, http);
    u.pending = false;
    return;
  }
  uint32_t wait = GATEWAY_UREC_RETRY_MS;
  for (uint8_t i = 1; i < u.attempts && wait < GATEWAY_UREC_MAX_RETRY_MS; i++) wait *= 2;
  if (wait > GATEWAY_UREC_MAX_RETRY_MS) wait = GATEWAY_UREC_MAX_RETRY_MS;
  u.lastTryMs = now;
  u.waitMs = wait;
  Serial.printf("[urec] %s: HTTP %d while %s, retry in %lu s\n", code, http,
                u.wantInUse ? "claiming" : "releasing", static_cast<unsigned long>(wait / 1000));
}

void syncUrec(uint32_t now) {
  if (!g_urec.configured()) return;
  for (size_t m = 0; m < kMachineCount; m++) {
    const UrecSync& u = g_urecSync[m];
    if (u.pending && elapsed(now, u.lastTryMs, u.waitMs)) syncUrecMachine(m, now);
  }
}

// Once per boot, after Wi-Fi first comes up: do the configured codes exist in the backend?
void bootChecks() {
  g_bootChecksDone = true;
  if (!g_urec.configured()) return;
  for (size_t m = 0; m < kMachineCount; m++) {
    String status;
    String name;
    const int http = g_urec.getStatus(kMachines[m].code, &status, &name);
    if (http == 200) {
      Serial.printf("[urec] %s = \"%s\" (%s)\n", kMachines[m].code, name.c_str(), status.c_str());
    } else {
      Serial.printf("[urec] %s: HTTP %d - check the code in gateway_config.h\n", kMachines[m].code, http);
    }
  }
}

}  // namespace

// ---------------------------------------------------------------- Arduino entry points

void setup() {
  Serial.begin(115200);
  delay(200);
  snprintf(g_bootId, sizeof(g_bootId), "%08lx", static_cast<unsigned long>(esp_random()));
  Serial.printf("\nGetMeFit gateway %s (boot %s)\n", GATEWAY_ID, g_bootId);
  for (size_t m = 0; m < kMachineCount; m++) {
    Serial.printf("  %s #%u -> %s (%s)\n", gym::equipmentName(kMachines[m].equipmentType),
                  kMachines[m].instance, kMachines[m].code, kMachines[m].name);
  }
  Serial.printf("  Firebase: %s, UREC: %s\n", g_firebase.configured() ? FIREBASE_DB_URL : "off",
                g_urec.configured() ? UREC_BASE_URL : "off");

  g_prefs.begin("urec", false);
  const uint32_t now = millis();
  for (size_t m = 0; m < kMachineCount; m++) {
    g_urecSync[m].claimed = g_prefs.getBool(kMachines[m].code, false);
    if (g_urecSync[m].claimed) {
      // We marked it "In Use" before this reboot. Release it after the idle time unless reps arrive.
      g_urecSync[m].wantInUse = false;
      g_urecSync[m].pending = true;
      g_urecSync[m].lastTryMs = now;
      g_urecSync[m].waitMs = GATEWAY_MACHINE_IDLE_MS;
      Serial.printf("[urec] %s was claimed before the reboot\n", kMachines[m].code);
    }
  }

  if (!ble_scanner::begin(GATEWAY_SCAN_INTERVAL_MS, GATEWAY_SCAN_WINDOW_MS, GATEWAY_PACKET_QUEUE_LEN)) {
    Serial.println("[ble] scanner failed to start");
  }
  g_firebase.begin();
  wifi_link::begin();
  printVector();
}

void loop() {
  BeaconPacket packet;
  while (ble_scanner::receive(&packet)) {
    const int m = findMachine(packet.equipmentType, packet.machineInstance);
    if (m < 0) {
      noteUnknownBeacon(packet);
      continue;
    }
    g_tracker.onPacket(static_cast<size_t>(m), packet.setSeq, packet.reps, packet.rssi, packet.ms);
    g_led.flash(0, 0, 40, 20);
  }

  const uint32_t now = millis();
  g_tracker.tick(now);

  bool vectorChanged = false;
  for (size_t m = 0; m < kMachineCount; m++) {
    if (g_tracker.takeLiveDirty(m)) {
      g_fbLiveDirty[m] = true;
      vectorChanged = true;
    }
    if (g_tracker.takeUseChanged(m)) onUseChanged(m, now);
  }
  gym::SetRecord rec;
  while (g_tracker.popSet(&rec)) queueSet(rec);
  if (vectorChanged) printVector();

  if (elapsed(now, g_lastScanCheckMs, kScanCheckMs)) {
    g_lastScanCheckMs = now;
    ble_scanner::ensureScanning();
  }

  wifi_link::update(now);
  if (wifi_link::connected()) {
    if (wifi_link::takeJustConnected() && !g_bootChecksDone) bootChecks();
    if (g_firebase.configured()) {
      uploadLive(now);
      uploadSets(now);
      uploadHeartbeat(now);
    }
    syncUrec(now);
  }

  g_led.update(millis());
  delay(10);
}
