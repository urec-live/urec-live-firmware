// The Feather nRF52840 Express's 2 MB QSPI flash (GD25Q16C), through the nRF52840's QSPI peripheral
// (nrfx driver, blocking mode, one data line, so the chip's quad-enable bit never matters).
//
// Between uses the peripheral is off and the chip is in deep power-down (about 1 uA). open() wakes
// both, close() puts both back. Every other call needs the flash open. The chip is woken by a
// command sent on the GPIOs before the peripheral starts: the peripheral cannot activate while the
// chip sleeps (qspi_flash.cpp).
#pragma once

#include <stddef.h>
#include <stdint.h>

class QspiFlash {
 public:
  static const uint32_t kPageBytes = 256;
  static const uint32_t kSectorBytes = 4096;
  static const uint32_t kBlockBytes = 65536;

  bool open();
  void close();
  bool isOpen() const { return open_; }

  // From the JEDEC id read at the first open(): the size in bytes, 0 if no flash answered.
  uint32_t capacity() const { return capacity_; }

  bool read(uint32_t addr, void* dst, size_t len);  // any address and length
  // `addr` and `len` multiples of 4, onto erased flash. Splits at page boundaries.
  bool write(uint32_t addr, const void* src, size_t len);
  bool eraseSector(uint32_t addr);  // the 4 KB sector holding addr
  bool eraseBlock(uint32_t addr);   // the 64 KB block holding addr
  bool isErased(uint32_t addr, size_t len);  // every byte reads 0xFF

 private:
  bool command(uint8_t opcode, uint8_t length, void* rx);
  bool waitReady(uint32_t timeoutMs);

  bool open_ = false;
  uint32_t capacity_ = 0;
};
