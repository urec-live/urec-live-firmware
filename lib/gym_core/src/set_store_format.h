// Record format of the beacon's set store (src/beacon/set_store.h): every recorded set is kept in
// the Feather's QSPI flash, so a whole session's CSV dumps can be downloaded later over USB.
// Plain C++11, no Arduino dependencies, so it is unit-tested with the rest of gym_core.
//
// A record is a 32-byte header, then the body: the set's "# ..." dump lines as text (metaBytes),
// then one 21-byte row per 50 Hz sample, padded with 0xFF to a multiple of 4 bytes. Records follow
// each other with no gap. The header is written LAST, so a record cut short by a power loss keeps
// an erased (all 0xFF) header and is never mistaken for a complete one.
//
//   [0..3]   magic "GMFS" (0x53464D47)            [16..17] metaBytes
//   [4..5]   version (1)                          [18..19] samples
//   [6..7]   header bytes (32)                    [20..21] bootId (random per power-up)
//   [8..11]  body bytes (meta + rows, unpadded)   [22..23] index (1, 2, ... since the last clear)
//   [12..15] CRC-32 of the body                   [24..27] beacon uptime when the set ended (ms)
//   [28] setSeq   [29] sensors (bit 0 TMAG5273, bit 1 LIS2MDL)   [30] counted reps   [31] row bytes
//
// A row: t_ms (u16), accelerometer x, y, z (i16, mg), TMAG5273 x, y, z and LIS2MDL x, y, z (i16,
// 0.1 uT), valid bits (u8). Everything is little-endian.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gym {

const uint32_t kStoreMagic = 0x53464D47;  // "GMFS" in memory order
const uint16_t kStoreVersion = 1;
const size_t kStoreHeaderBytes = 32;
const size_t kStoreRowBytes = 21;

struct StoredSetHeader {
  uint32_t bodyBytes;  // meta + rows, without the padding
  uint32_t crc;        // CRC-32 of the body
  uint16_t metaBytes;
  uint16_t samples;
  uint16_t bootId;
  uint16_t index;
  uint32_t uptimeMs;
  uint8_t setSeq;
  uint8_t sensors;
  uint8_t reps;
};

struct StoredRow {
  uint16_t tMs;
  int16_t acc[3];
  int16_t mag[2][3];  // [0] TMAG5273, [1] LIS2MDL
  uint8_t valid;
};

void encodeStoredHeader(const StoredSetHeader& h, uint8_t out[kStoreHeaderBytes]);

// False unless `in` is a complete header of this version, consistent with itself, whose record
// fits in `spaceLeft` bytes. An erased header (all 0xFF) is never valid.
bool decodeStoredHeader(const uint8_t in[kStoreHeaderBytes], size_t spaceLeft, StoredSetHeader* out);

// Bytes a record takes in flash: the header plus the body, padded to a multiple of 4.
size_t storedRecordBytes(uint32_t bodyBytes);

void encodeStoredRow(const StoredRow& r, uint8_t out[kStoreRowBytes]);
void decodeStoredRow(const uint8_t in[kStoreRowBytes], StoredRow* out);

// CRC-32 as zlib's crc32() (IEEE 802.3): start with 0 and feed the bytes in any number of calls.
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len);

}  // namespace gym
