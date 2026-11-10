// Every recorded set's CSV dump, kept in the Feather's 2 MB QSPI flash (qspi_flash.h), so a whole
// session can be downloaded afterwards over USB: `dumpall`, or tools/download_sets.py. The beacon
// can then run on its battery all session, with a phone as its console.
//
// Records (lib/gym_core/src/set_store_format.h) are appended one after the other and survive
// power-offs; `clearsets` empties the store. When it is full, new sets are not stored and the
// oldest are never overwritten. A typical 8-rep set takes ~25 KB, so 2 MB holds about 80.
//
// Writing a set takes about 0.1 s right after it ends, while the rep counter is idle; a rep in
// that time is still counted (its interrupt waits), only a moment late. A record cut short by a
// power loss is skipped: the next record starts on a fresh 4 KB sector, and the boot scan steps
// over the damaged one.
#pragma once

#include <Arduino.h>

#include "qspi_flash.h"
#include "set_store_format.h"

class SetStore {
 public:
  // Finds the flash and the end of the stored records. bootId: random, tells power-ups apart.
  bool begin(uint16_t bootId);
  bool ready() const { return ready_; }
  uint16_t count() const { return count_; }
  uint32_t usedBytes() const { return end_; }
  uint32_t capacity() const { return flash_.capacity(); }
  bool skippedDamage() const { return skipped_; }

  // One record: beginRecord() with its body size returns why it cannot be stored, or nullptr;
  // then append() the body in any number of pieces, then commit() (which fills in the body size,
  // CRC, boot id and index, and writes the header last). abortRecord() after a failure.
  const char* beginRecord(uint32_t bodyBytes);
  bool append(const void* data, size_t len);
  bool commit(gym::StoredSetHeader h);
  void abortRecord();

  // "store: 12 sets, 300 of 2048 KB used", then the newest `maxRows` records.
  void printList(Print& out, uint16_t maxRows);

  // Erases every stored set. Blocks for up to ~10 s while it erases.
  bool clear(Print& out);

  // `dumpall`: every record as CSV, on Serial, a few lines per call from loop() while the USB
  // buffer has room (the same rule as the live dump).
  bool startDump();
  bool dumping() const { return dump_ != kIdle; }
  void pumpDump();

 private:
  enum DumpState : uint8_t { kIdle, kHeader, kMeta, kColumns, kRows, kCrc, kEnd };

  bool ensureBlank(uint32_t to);
  bool flushPage();
  bool writeFlash(uint32_t addr, const void* data, size_t len);
  void stopDump();

  QspiFlash flash_;
  bool ready_ = false;
  bool skipped_ = false;      // the scan stepped over a damaged record or older data
  uint16_t bootId_ = 0;
  uint16_t count_ = 0;
  uint32_t end_ = 0;          // where the next record goes
  uint32_t blankUpTo_ = 0;    // [end_, blankUpTo_) is known to be erased; always sector-aligned

  // The record being written.
  bool writing_ = false;
  bool programmed_ = false;   // some of it may be in the flash already
  uint32_t rec_ = 0;
  uint32_t recBody_ = 0;
  uint32_t recDone_ = 0;
  uint32_t recCrc_ = 0;
  uint32_t lastWriteEnd_ = 0;
  uint32_t pbAddr_ = 0;       // flash address of pb_[0]
  size_t pbLen_ = 0;
  uint8_t pb_[QspiFlash::kPageBytes];

  // dumpall
  DumpState dump_ = kIdle;
  uint32_t dumpOff_ = 0;
  uint32_t dumpPos_ = 0;
  uint32_t dumpCrc_ = 0;
  uint16_t dumpSets_ = 0;
  gym::StoredSetHeader dumpHdr_ = {};
};
