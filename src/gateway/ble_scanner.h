// Continuous passive BLE scan for GetMeFit iBeacons (NimBLE-Arduino 2.x).
//
// NimBLE calls onResult() in its own host task for every advertisement. The callback only
// decodes and copies the packet into a FreeRTOS queue; loop() drains the queue. So scanning
// keeps running while loop() is busy uploading - unlike the paper's gateway, which alternated
// 3 s of scanning with 0.9 s of uploading and missed packets.
#pragma once

#include <stddef.h>
#include <stdint.h>

struct BeaconPacket {
  uint16_t equipmentType;
  uint8_t machineInstance;
  uint8_t setSeq;
  uint16_t reps;
  int8_t rssi;
  uint32_t ms;  // millis() when received
};

namespace ble_scanner {

bool begin(uint16_t intervalMs, uint16_t windowMs, size_t queueLength);

// Non-blocking: returns false when no packet is waiting.
bool receive(BeaconPacket* out);

// Restarts the scan if it stopped for any reason.
void ensureScanning();

uint32_t packetsSeen();     // GetMeFit packets decoded
uint32_t packetsDropped();  // queue was full: superseded packets of the same beacon/set merged away

}  // namespace ble_scanner
