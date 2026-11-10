#include "gym_protocol.h"

#include <string.h>

namespace gym {

const uint8_t kUuidBase[14] = {0x6D, 0x1E, 0x3F, 0x2A, 0x9B, 0x47, 0x4C,
                               0x85, 0xA0, 0xE2, 0x7F, 0x31, 0xC5, 0x58};

const char* equipmentName(uint16_t type) {
  switch (type) {
    case kEquipLegCurl: return "Leg Curl";
    case kEquipLegExtension: return "Leg Extension";
    case kEquipLatPulldown: return "Lat Pulldown";
    case kEquipSeatedRow: return "Seated Row";
    case kEquipChestFly: return "Chest Fly";
    case kEquipShoulderPress: return "Shoulder Press";
    case kEquipBicepCurl: return "Bicep Curl";
    case kEquipTricepPress: return "Tricep Press";
    case kEquipCableStation: return "Cable Station";
    case kEquipCalfRaise: return "Calf Raise";
    case kEquipAbCrunch: return "Ab Crunch";
    case kEquipRearDeltFly: return "Rear Delt Fly";
    case kEquipGluteKickback: return "Glute Kickback";
    case kEquipLateralRaise: return "Lateral Raise";
    default: return "Unknown";
  }
}

void encodeIBeacon(const BeaconFrame& frame, uint8_t out[kIBeaconDataLen]) {
  out[0] = static_cast<uint8_t>(kIBeaconCompanyId & 0xFF);
  out[1] = static_cast<uint8_t>(kIBeaconCompanyId >> 8);
  out[2] = kIBeaconType;
  out[3] = kIBeaconLength;
  memcpy(&out[4], kUuidBase, sizeof(kUuidBase));
  out[18] = static_cast<uint8_t>(frame.equipmentType >> 8);
  out[19] = static_cast<uint8_t>(frame.equipmentType & 0xFF);
  out[20] = frame.setSeq;           // major, big-endian: high byte
  out[21] = frame.machineInstance;  // major, big-endian: low byte
  out[22] = static_cast<uint8_t>(frame.reps >> 8);
  out[23] = static_cast<uint8_t>(frame.reps & 0xFF);
  out[24] = static_cast<uint8_t>(frame.txPower);
}

bool decodeIBeacon(const uint8_t* data, size_t len, BeaconFrame* out) {
  if (data == nullptr || len != kIBeaconDataLen) return false;
  if (data[0] != (kIBeaconCompanyId & 0xFF) || data[1] != (kIBeaconCompanyId >> 8)) return false;
  if (data[2] != kIBeaconType || data[3] != kIBeaconLength) return false;
  if (memcmp(&data[4], kUuidBase, sizeof(kUuidBase)) != 0) return false;
  if (out != nullptr) {
    out->equipmentType = static_cast<uint16_t>((data[18] << 8) | data[19]);
    out->setSeq = data[20];
    out->machineInstance = data[21];
    out->reps = static_cast<uint16_t>((data[22] << 8) | data[23]);
    out->txPower = static_cast<int8_t>(data[24]);
  }
  return true;
}

namespace {

// Walks the AD structures and returns the first manufacturer data of `dataLen` bytes that
// `decode` accepts.
template <typename Frame>
bool findInPayload(const uint8_t* payload, size_t len, size_t dataLen,
                   bool (*decode)(const uint8_t*, size_t, Frame*), Frame* out) {
  if (payload == nullptr) return false;
  size_t i = 0;
  while (i < len) {
    const size_t fieldLen = payload[i];  // counts the type byte + data
    if (fieldLen == 0) return false;     // zero padding ends the payload
    if (i + 1 + fieldLen > len) return false;
    const uint8_t type = payload[i + 1];
    if (type == kAdTypeManufacturerData && fieldLen - 1 == dataLen && decode(&payload[i + 2], dataLen, out)) {
      return true;
    }
    i += 1 + fieldLen;
  }
  return false;
}

}  // namespace

bool findIBeaconInPayload(const uint8_t* payload, size_t len, BeaconFrame* out) {
  return findInPayload(payload, len, kIBeaconDataLen, decodeIBeacon, out);
}

void encodeStatus(const StatusFrame& frame, uint8_t out[kStatusDataLen]) {
  out[0] = static_cast<uint8_t>(kStatusCompanyId & 0xFF);
  out[1] = static_cast<uint8_t>(kStatusCompanyId >> 8);
  memcpy(&out[2], kUuidBase, 4);
  out[6] = kStatusKind;
  out[7] = static_cast<uint8_t>(frame.equipmentType >> 8);
  out[8] = static_cast<uint8_t>(frame.equipmentType & 0xFF);
  out[9] = frame.machineInstance;
  out[10] = static_cast<uint8_t>(frame.batteryMv >> 8);
  out[11] = static_cast<uint8_t>(frame.batteryMv & 0xFF);
  out[12] = frame.flags;
}

bool decodeStatus(const uint8_t* data, size_t len, StatusFrame* out) {
  if (data == nullptr || len != kStatusDataLen) return false;
  if (data[0] != (kStatusCompanyId & 0xFF) || data[1] != (kStatusCompanyId >> 8)) return false;
  if (memcmp(&data[2], kUuidBase, 4) != 0 || data[6] != kStatusKind) return false;
  if (out != nullptr) {
    out->equipmentType = static_cast<uint16_t>((data[7] << 8) | data[8]);
    out->machineInstance = data[9];
    out->batteryMv = static_cast<uint16_t>((data[10] << 8) | data[11]);
    out->flags = data[12];
  }
  return true;
}

bool findStatusInPayload(const uint8_t* payload, size_t len, StatusFrame* out) {
  return findInPayload(payload, len, kStatusDataLen, decodeStatus, out);
}

uint8_t batteryPercent(uint16_t mv) {
  // Resting voltage of a LiPo cell at a very light load against charge left (typical curve).
  static const uint16_t kMv[] = {3270, 3610, 3690, 3730, 3770, 3800, 3840, 3870, 3950, 4020, 4110, 4200};
  static const uint8_t kPct[] = {0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
  const size_t n = sizeof(kMv) / sizeof(kMv[0]);
  if (mv <= kMv[0]) return 0;
  if (mv >= kMv[n - 1]) return 100;
  size_t i = 1;
  while (mv > kMv[i]) i++;
  // Linear between the two points, rounded to the nearest percent.
  const uint32_t span = kMv[i] - kMv[i - 1];
  const uint32_t step = kPct[i] - kPct[i - 1];
  return static_cast<uint8_t>(kPct[i - 1] + ((mv - kMv[i - 1]) * step + span / 2) / span);
}

}  // namespace gym
