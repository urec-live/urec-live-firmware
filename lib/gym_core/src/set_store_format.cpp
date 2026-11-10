#include "set_store_format.h"

namespace gym {

namespace {

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t* p, uint32_t v) {
  put16(p, static_cast<uint16_t>(v));
  put16(p + 2, static_cast<uint16_t>(v >> 16));
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t get32(const uint8_t* p) { return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16); }

}  // namespace

void encodeStoredHeader(const StoredSetHeader& h, uint8_t out[kStoreHeaderBytes]) {
  put32(out + 0, kStoreMagic);
  put16(out + 4, kStoreVersion);
  put16(out + 6, static_cast<uint16_t>(kStoreHeaderBytes));
  put32(out + 8, h.bodyBytes);
  put32(out + 12, h.crc);
  put16(out + 16, h.metaBytes);
  put16(out + 18, h.samples);
  put16(out + 20, h.bootId);
  put16(out + 22, h.index);
  put32(out + 24, h.uptimeMs);
  out[28] = h.setSeq;
  out[29] = h.sensors;
  out[30] = h.reps;
  out[31] = static_cast<uint8_t>(kStoreRowBytes);
}

bool decodeStoredHeader(const uint8_t in[kStoreHeaderBytes], size_t spaceLeft, StoredSetHeader* out) {
  if (get32(in + 0) != kStoreMagic || get16(in + 4) != kStoreVersion || get16(in + 6) != kStoreHeaderBytes ||
      in[31] != kStoreRowBytes) {
    return false;
  }
  StoredSetHeader h;
  h.bodyBytes = get32(in + 8);
  h.crc = get32(in + 12);
  h.metaBytes = get16(in + 16);
  h.samples = get16(in + 18);
  h.bootId = get16(in + 20);
  h.index = get16(in + 22);
  h.uptimeMs = get32(in + 24);
  h.setSeq = in[28];
  h.sensors = in[29];
  h.reps = in[30];
  const uint32_t expect = static_cast<uint32_t>(h.metaBytes) + static_cast<uint32_t>(h.samples) * kStoreRowBytes;
  if (h.bodyBytes == 0 || h.bodyBytes != expect) return false;
  if (storedRecordBytes(h.bodyBytes) > spaceLeft) return false;
  *out = h;
  return true;
}

size_t storedRecordBytes(uint32_t bodyBytes) { return kStoreHeaderBytes + ((bodyBytes + 3u) & ~3u); }

void encodeStoredRow(const StoredRow& r, uint8_t out[kStoreRowBytes]) {
  put16(out, r.tMs);
  for (int i = 0; i < 3; i++) put16(out + 2 + 2 * i, static_cast<uint16_t>(r.acc[i]));
  for (int k = 0; k < 2; k++) {
    for (int i = 0; i < 3; i++) put16(out + 8 + 6 * k + 2 * i, static_cast<uint16_t>(r.mag[k][i]));
  }
  out[20] = r.valid;
}

void decodeStoredRow(const uint8_t in[kStoreRowBytes], StoredRow* out) {
  out->tMs = get16(in);
  for (int i = 0; i < 3; i++) out->acc[i] = static_cast<int16_t>(get16(in + 2 + 2 * i));
  for (int k = 0; k < 2; k++) {
    for (int i = 0; i < 3; i++) out->mag[k][i] = static_cast<int16_t>(get16(in + 8 + 6 * k + 2 * i));
  }
  out->valid = in[20];
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

}  // namespace gym
