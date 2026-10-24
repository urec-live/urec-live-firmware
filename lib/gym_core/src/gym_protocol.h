// GetMeFit beacon protocol: the iBeacon frame shared by the beacon (nRF52840) and the
// gateway (ESP32-C3). Plain C++11, no Arduino dependencies, so it is unit-tested on both boards.
//
// Manufacturer-specific data (25 bytes, exactly as it goes on air):
//
//   [0..1]   4C 00        Apple company ID 0x004C, little-endian (iBeacon convention)
//   [2]      02           iBeacon type
//   [3]      15           remaining length (21)
//   [4..17]  UUID base    14 fixed bytes identifying GetMeFit beacons
//   [18..19] type         equipment type, big-endian  -> "UUID = equipment type" (paper)
//   [20]     setSeq       major high byte: set sequence number, 1..255
//   [21]     instance     major low byte: which machine of that type (1, 2, ...)
//   [22..23] reps         minor, big-endian           -> "minor = repetition count" (paper)
//   [24]     txPower      calibrated RSSI at 1 m (signed dBm)
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gym {

const uint16_t kIBeaconCompanyId = 0x004C;
const uint8_t kIBeaconType = 0x02;
const uint8_t kIBeaconLength = 0x15;
const size_t kIBeaconDataLen = 25;
const uint8_t kAdTypeManufacturerData = 0xFF;

// First 14 bytes of every GetMeFit beacon UUID (random v4 UUID 6D1E3F2A-9B47-4C85-A0E2-7F31C558xxxx).
extern const uint8_t kUuidBase[14];

// Equipment types carried in the last two UUID bytes. Values are part of the radio protocol:
// append new ones, never renumber.
enum EquipmentType : uint16_t {
  kEquipUnknown = 0,
  kEquipLegCurl = 1,
  kEquipLegExtension = 2,
  kEquipLatPulldown = 3,
  kEquipSeatedRow = 4,
  kEquipChestFly = 5,
  kEquipShoulderPress = 6,
  kEquipBicepCurl = 7,
  kEquipTricepPress = 8,
  kEquipCableStation = 9,
  kEquipCalfRaise = 10,
  kEquipAbCrunch = 11,
  kEquipRearDeltFly = 12,
  kEquipGluteKickback = 13,
  kEquipLateralRaise = 14,
};

// Human-readable name for logs ("Leg Curl"), "Unknown" for anything not listed.
const char* equipmentName(uint16_t type);

struct BeaconFrame {
  uint16_t equipmentType;
  uint8_t machineInstance;
  uint8_t setSeq;
  uint16_t reps;
  int8_t txPower;
};

inline uint16_t packMajor(uint8_t setSeq, uint8_t machineInstance) {
  return static_cast<uint16_t>((static_cast<uint16_t>(setSeq) << 8) | machineInstance);
}

// Next set sequence number after `seq`: 1..255, never 0 (0 means "no set yet").
inline uint8_t nextSetSeq(uint8_t seq) { return seq == 255 ? 1 : static_cast<uint8_t>(seq + 1); }

// Writes the 25-byte manufacturer data for `frame` into `out`.
void encodeIBeacon(const BeaconFrame& frame, uint8_t out[kIBeaconDataLen]);

// Decodes 25 bytes of manufacturer data. Returns false unless it is a GetMeFit iBeacon.
bool decodeIBeacon(const uint8_t* data, size_t len, BeaconFrame* out);

// Walks the AD structures of a raw advertising payload ([len][type][data...]...) and decodes
// the first GetMeFit iBeacon found. Returns false if there is none or the payload is malformed.
bool findIBeaconInPayload(const uint8_t* payload, size_t len, BeaconFrame* out);

}  // namespace gym
