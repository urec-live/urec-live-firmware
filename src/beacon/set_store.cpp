#include "set_store.h"

#include <string.h>

namespace {

const uint32_t kSector = QspiFlash::kSectorBytes;
const char kColumnHeader[] = "t_ms,ax_mg,ay_mg,az_mg,tx,ty,tz,lx,ly,lz,valid\n";

// The boundary that ends the sector holding `addr`.
uint32_t sectorEnd(uint32_t addr) { return (addr / kSector + 1) * kSector; }

uint32_t alignUpSector(uint32_t addr) { return (addr + kSector - 1) / kSector * kSector; }

}  // namespace

bool SetStore::begin(uint16_t bootId) {
  bootId_ = bootId;
  ready_ = false;
  if (!flash_.open()) return false;  // no flash: the sets are not stored
  const uint32_t cap = flash_.capacity();
  uint32_t off = 0;
  uint16_t n = 0;
  bool readOk = true;
  while (off + gym::kStoreHeaderBytes <= cap) {
    uint8_t raw[gym::kStoreHeaderBytes];
    if (!flash_.read(off, raw, sizeof(raw))) {
      readOk = false;
      break;
    }
    gym::StoredSetHeader h;
    if (gym::decodeStoredHeader(raw, cap - off, &h)) {
      off += gym::storedRecordBytes(h.bodyBytes);
      n++;
      continue;
    }
    const uint32_t se = sectorEnd(off) < cap ? sectorEnd(off) : cap;
    if (flash_.isErased(off, se - off)) break;  // the end of the records
    skipped_ = true;  // a record cut short by a power loss, or data from before the store
    off = se;
  }
  flash_.close();
  if (!readOk) return false;
  end_ = off < cap ? off : cap;
  count_ = n;
  blankUpTo_ = sectorEnd(end_) < cap ? sectorEnd(end_) : cap;
  ready_ = true;
  return true;
}

// ---------------------------------------------------------------- writing a record

bool SetStore::ensureBlank(uint32_t to) {
  while (blankUpTo_ < to) {
    const uint32_t s = blankUpTo_;
    if (!flash_.isErased(s, kSector) && !flash_.eraseSector(s)) return false;
    blankUpTo_ = s + kSector;
  }
  return true;
}

bool SetStore::writeFlash(uint32_t addr, const void* data, size_t len) {
  programmed_ = true;
  if (addr + len > lastWriteEnd_) lastWriteEnd_ = addr + len;
  return flash_.write(addr, data, len);
}

const char* SetStore::beginRecord(uint32_t bodyBytes) {
  if (!ready_) return "no flash for the set store";
  if (writing_) abortRecord();
  if (dumping()) return "a download (dumpall) is running";
  if (bodyBytes == 0) return "nothing was recorded";
  const uint32_t total = gym::storedRecordBytes(bodyBytes);
  if (end_ + total > flash_.capacity()) return "the store is full - download the sets, then clearsets";
  if (!flash_.open()) return "the flash did not answer";
  if (!ensureBlank(end_ + total)) {
    flash_.close();
    return "the flash could not be erased";
  }
  writing_ = true;
  programmed_ = false;
  rec_ = end_;
  recBody_ = bodyBytes;
  recDone_ = 0;
  recCrc_ = 0;
  lastWriteEnd_ = rec_;
  pbAddr_ = rec_ + gym::kStoreHeaderBytes;
  pbLen_ = 0;
  return nullptr;
}

bool SetStore::flushPage() {
  if (pbLen_ == 0) return true;
  while (pbLen_ % 4 != 0) pb_[pbLen_++] = 0xFF;  // only a record's last piece can be short
  if (!writeFlash(pbAddr_, pb_, pbLen_)) return false;
  pbAddr_ += pbLen_;
  pbLen_ = 0;
  return true;
}

bool SetStore::append(const void* data, size_t len) {
  if (!writing_ || recDone_ + len > recBody_) return false;
  const uint8_t* p = static_cast<const uint8_t*>(data);
  recCrc_ = gym::crc32Update(recCrc_, p, len);
  recDone_ += len;
  while (len > 0) {
    // Fill up to the end of the flash page, so each write is one page program.
    const size_t room = QspiFlash::kPageBytes - (pbAddr_ % QspiFlash::kPageBytes);
    size_t n = room - pbLen_;
    if (n > len) n = len;
    memcpy(pb_ + pbLen_, p, n);
    pbLen_ += n;
    p += n;
    len -= n;
    if (pbLen_ == room && !flushPage()) return false;
  }
  return true;
}

bool SetStore::commit(gym::StoredSetHeader h) {
  if (!writing_ || recDone_ != recBody_ || !flushPage()) {
    abortRecord();
    return false;
  }
  h.bodyBytes = recBody_;
  h.crc = recCrc_;
  h.bootId = bootId_;
  h.index = static_cast<uint16_t>(count_ + 1);
  uint8_t raw[gym::kStoreHeaderBytes];
  uint8_t back[gym::kStoreHeaderBytes];
  gym::encodeStoredHeader(h, raw);
  // The header goes last: until it is in, the record does not exist for the scan.
  if (!writeFlash(rec_, raw, sizeof(raw)) || !flash_.read(rec_, back, sizeof(back)) ||
      memcmp(raw, back, sizeof(raw)) != 0) {
    abortRecord();
    return false;
  }
  end_ = rec_ + gym::storedRecordBytes(recBody_);
  count_++;
  writing_ = false;
  flash_.close();
  return true;
}

void SetStore::abortRecord() {
  if (!writing_) return;
  writing_ = false;
  if (programmed_) {
    // Part of this record may be in the flash. The next one starts on a fresh sector after it,
    // where the boot scan finds it again after stepping over this one.
    end_ = sectorEnd(lastWriteEnd_ - 1);
    skipped_ = true;
  }
  flash_.close();
}

// ---------------------------------------------------------------- listing and clearing

void SetStore::printList(Print& out, uint16_t maxRows) {
  if (!ready_) {
    out.println(F("store: no QSPI flash found - sets are not stored"));
    return;
  }
  out.printf("store: %u sets, %lu of %lu KB used%s\n", count_, static_cast<unsigned long>((end_ + 1023) / 1024),
             static_cast<unsigned long>(capacity() / 1024), skipped_ ? " (stepped over damaged data)" : "");
  if (count_ == 0 || maxRows == 0) return;
  if (writing_ || dumping() || !flash_.open()) {
    out.println(F("  (busy - list them later)"));
    return;
  }
  const uint16_t first = count_ > maxRows ? count_ - maxRows : 0;
  if (first > 0) out.printf("  ... %u older sets not listed\n", first);
  uint32_t off = 0;
  uint16_t i = 0;
  while (off + gym::kStoreHeaderBytes <= end_ && i < count_) {
    uint8_t raw[gym::kStoreHeaderBytes];
    gym::StoredSetHeader h;
    if (!flash_.read(off, raw, sizeof(raw))) break;
    if (!gym::decodeStoredHeader(raw, capacity() - off, &h)) {
      off = sectorEnd(off);  // the same step over damaged data as the boot scan
      continue;
    }
    if (i >= first) {
      out.printf("  #%u  set %u  %u reps  %u samples  boot %04X, %lu s after power-up\n", h.index, h.setSeq,
                 h.reps, h.samples, h.bootId, static_cast<unsigned long>(h.uptimeMs / 1000));
    }
    i++;
    off += gym::storedRecordBytes(h.bodyBytes);
  }
  flash_.close();
}

bool SetStore::clear(Print& out) {
  if (!ready_ || writing_ || dumping()) return false;
  const uint32_t to = alignUpSector(end_);
  if (to > 0) {
    if (!flash_.open()) return false;
    out.printf("[store] erasing %lu KB ...\n", static_cast<unsigned long>(to / 1024));
    bool ok = true;
    uint32_t addr = 0;
    while (ok && addr < to) {
      if (addr % QspiFlash::kBlockBytes == 0 && addr + QspiFlash::kBlockBytes <= to) {
        ok = flash_.eraseBlock(addr);
        addr += QspiFlash::kBlockBytes;
      } else {
        ok = flash_.eraseSector(addr);
        addr += kSector;
      }
    }
    flash_.close();
    if (!ok) return false;
    if (blankUpTo_ < to) blankUpTo_ = to;
  }
  end_ = 0;
  count_ = 0;
  skipped_ = false;
  return true;
}

// ---------------------------------------------------------------- dumpall

bool SetStore::startDump() {
  if (!ready_ || writing_ || dumping() || count_ == 0 || !flash_.open()) return false;
  dump_ = kHeader;
  dumpOff_ = 0;
  dumpSets_ = 0;
  return true;
}

void SetStore::stopDump() {
  dump_ = kIdle;
  flash_.close();
}

void SetStore::pumpDump() {
  if (dump_ == kIdle) return;
  if (!Serial) {  // the monitor went away
    stopDump();
    return;
  }
  char line[128];
  for (uint8_t lines = 0; lines < 32 && dump_ != kIdle && Serial.availableForWrite() >= 128; lines++) {
    switch (dump_) {
      case kHeader: {
        uint8_t raw[gym::kStoreHeaderBytes];
        bool found = false;
        while (dumpOff_ + gym::kStoreHeaderBytes <= end_) {
          if (!flash_.read(dumpOff_, raw, sizeof(raw))) break;
          if (gym::decodeStoredHeader(raw, capacity() - dumpOff_, &dumpHdr_)) {
            found = true;
            break;
          }
          dumpOff_ = sectorEnd(dumpOff_);  // the same step over damaged data as the boot scan
        }
        if (!found) {
          snprintf(line, sizeof(line), "# stored end: %u sets\n", dumpSets_);
          Serial.print(line);
          stopDump();
          break;
        }
        snprintf(line, sizeof(line), "# stored index=%u boot=%04X set=%u reps=%u samples=%u uptime_ms=%lu\n",
                 dumpHdr_.index, dumpHdr_.bootId, dumpHdr_.setSeq, dumpHdr_.reps, dumpHdr_.samples,
                 static_cast<unsigned long>(dumpHdr_.uptimeMs));
        Serial.print(line);
        dumpPos_ = 0;
        dumpCrc_ = 0;
        dump_ = kMeta;
        break;
      }
      case kMeta: {  // the stored "# ..." lines, as they were
        const uint32_t left = dumpHdr_.metaBytes - dumpPos_;
        if (left == 0) {
          dump_ = kColumns;
          break;
        }
        uint8_t buf[96];
        const size_t n = left < sizeof(buf) ? left : sizeof(buf);
        if (!flash_.read(dumpOff_ + gym::kStoreHeaderBytes + dumpPos_, buf, n)) {
          Serial.print(F("\n# dump aborted: flash read failed\n"));
          stopDump();
          break;
        }
        dumpCrc_ = gym::crc32Update(dumpCrc_, buf, n);
        Serial.write(buf, n);
        dumpPos_ += n;
        break;
      }
      case kColumns:
        Serial.print(kColumnHeader);  // (not the enum value kColumns)
        dumpPos_ = 0;  // from here on: rows
        dump_ = kRows;
        break;
      case kRows: {
        if (dumpPos_ >= dumpHdr_.samples) {
          dump_ = kCrc;
          break;
        }
        uint8_t raw[gym::kStoreRowBytes];
        const uint32_t at = dumpOff_ + gym::kStoreHeaderBytes + dumpHdr_.metaBytes + dumpPos_ * gym::kStoreRowBytes;
        if (!flash_.read(at, raw, sizeof(raw))) {
          Serial.print(F("# dump aborted: flash read failed\n"));
          stopDump();
          break;
        }
        dumpCrc_ = gym::crc32Update(dumpCrc_, raw, sizeof(raw));
        gym::StoredRow r;
        gym::decodeStoredRow(raw, &r);
        snprintf(line, sizeof(line), "%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u\n", r.tMs, r.acc[0], r.acc[1], r.acc[2],
                 r.mag[0][0], r.mag[0][1], r.mag[0][2], r.mag[1][0], r.mag[1][1], r.mag[1][2], r.valid);
        Serial.print(line);
        dumpPos_++;
        break;
      }
      case kCrc:
        Serial.print(dumpCrc_ == dumpHdr_.crc ? "# crc=ok\n" : "# crc=bad\n");
        dump_ = kEnd;
        break;
      case kEnd:
        Serial.print("# end\n");
        dumpSets_++;
        dumpOff_ += gym::storedRecordBytes(dumpHdr_.bodyBytes);
        dump_ = kHeader;
        break;
      case kIdle:
      default:
        stopDump();
        break;
    }
  }
}
