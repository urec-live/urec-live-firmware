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

bool findIBeaconInPayload(const uint8_t* payload, size_t len, BeaconFrame* out) {
  if (payload == nullptr) return false;
  size_t i = 0;
  while (i < len) {
    const size_t fieldLen = payload[i];  // counts the type byte + data
    if (fieldLen == 0) return false;     // zero padding ends the payload
    if (i + 1 + fieldLen > len) return false;
    const uint8_t type = payload[i + 1];
    if (type == kAdTypeManufacturerData && fieldLen - 1 == kIBeaconDataLen &&
        decodeIBeacon(&payload[i + 2], kIBeaconDataLen, out)) {
      return true;
    }
    i += 1 + fieldLen;
  }
  return false;
}

}  // namespace gym
