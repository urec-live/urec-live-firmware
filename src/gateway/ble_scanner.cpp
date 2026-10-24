#include "ble_scanner.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "gym_protocol.h"

namespace {

QueueHandle_t g_queue = nullptr;
volatile uint32_t g_seen = 0;
volatile uint32_t g_dropped = 0;
BeaconPacket* g_scratch = nullptr;  // compaction buffer, only used inside the NimBLE host task
size_t g_scratchLen = 0;

bool sameSet(const BeaconPacket& a, const BeaconPacket& b) {
  return a.equipmentType == b.equipmentType && a.machineInstance == b.machineInstance &&
         a.setSeq == b.setSeq;
}

// Adds `p` to the first `n` scratch entries: a newer packet of the same beacon and set replaces the
// older one (counts are cumulative, so it carries everything the old one did).
void mergeInto(size_t* n, const BeaconPacket& p) {
  for (size_t i = 0; i < *n; i++) {
    if (sameSet(g_scratch[i], p)) {
      g_scratch[i] = p;
      g_dropped = g_dropped + 1;
      return;
    }
  }
  if (*n < g_scratchLen) {
    g_scratch[(*n)++] = p;
  } else {
    g_dropped = g_dropped + 1;  // only if every slot holds a different beacon/set
  }
}

// Queue full (loop() stuck in a slow upload): shrink it to the newest packet per beacon and set,
// so neither a busy beacon nor one that went quiet loses its latest count. Non-blocking.
void compactAndSend(const BeaconPacket& packet) {
  size_t n = 0;
  BeaconPacket p;
  while (xQueueReceive(g_queue, &p, 0) == pdTRUE) mergeInto(&n, p);
  mergeInto(&n, packet);
  for (size_t i = 0; i < n; i++) {
    if (xQueueSend(g_queue, &g_scratch[i], 0) != pdTRUE) g_dropped = g_dropped + 1;
  }
}

class ScanCallbacks : public NimBLEScanCallbacks {
  // Runs in the NimBLE host task: no Serial, no HTTP, no blocking.
  void onResult(const NimBLEAdvertisedDevice* device) override {
    const std::vector<uint8_t>& payload = device->getPayload();  // walked in place, no copies
    gym::BeaconFrame frame;
    if (!gym::findIBeaconInPayload(payload.data(), payload.size(), &frame)) return;
    BeaconPacket packet;
    packet.equipmentType = frame.equipmentType;
    packet.machineInstance = frame.machineInstance;
    packet.setSeq = frame.setSeq;
    packet.reps = frame.reps;
    packet.rssi = device->getRSSI();
    packet.ms = millis();
    g_seen = g_seen + 1;
    if (xQueueSend(g_queue, &packet, 0) != pdTRUE) compactAndSend(packet);
  }
};

ScanCallbacks g_callbacks;

}  // namespace

namespace ble_scanner {

bool begin(uint16_t intervalMs, uint16_t windowMs, size_t queueLength) {
  g_queue = xQueueCreate(queueLength, sizeof(BeaconPacket));
  g_scratch = new BeaconPacket[queueLength];
  g_scratchLen = queueLength;
  if (g_queue == nullptr || g_scratch == nullptr) return false;
  if (!NimBLEDevice::init("")) return false;
  NimBLEScan* scan = NimBLEDevice::getScan();
  // wantDuplicates = true turns the controller's duplicate filter off. With it on, a beacon is
  // reported once per scan and every later packet - with a new rep count - would be dropped.
  scan->setScanCallbacks(&g_callbacks, true);
  scan->setActiveScan(false);  // passive: beacons are non-scannable, nothing to ask for
  scan->setInterval(intervalMs);
  scan->setWindow(windowMs);
  scan->setMaxResults(0);      // callbacks only, keep no result list
  return scan->start(0, false, true);  // 0 = scan forever (NimBLE 2.x times are in ms)
}

bool receive(BeaconPacket* out) {
  return g_queue != nullptr && xQueueReceive(g_queue, out, 0) == pdTRUE;
}

void ensureScanning() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (!scan->isScanning()) scan->start(0, false, true);
}

uint32_t packetsSeen() { return g_seen; }
uint32_t packetsDropped() { return g_dropped; }

}  // namespace ble_scanner
