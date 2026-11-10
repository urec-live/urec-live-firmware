// Where console text goes: the USB serial monitor, a phone on the Bluetooth console
// (ble_console.h), or both, whichever is listening. Everything the console prints goes through
// `Console`; only the CSV dumps and the Teleplot stream write to Serial directly, because they are
// too long for the Bluetooth link.
#pragma once

#include <Arduino.h>

class ConsoleOut : public Print {
 public:
  static const uint8_t kUsb = 1;
  static const uint8_t kPhone = 2;
  static const uint8_t kBoth = kUsb | kPhone;

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buffer, size_t size) override;
  using Print::write;

  // Someone is listening (within the current restriction): `if (Console) Console.printf(...)`.
  explicit operator bool() const { return listeners() != 0; }
  uint8_t listeners() const;

  // Restricts the output to some listeners while it exists, e.g. the banner for a phone that has
  // just connected, which the USB monitor has already seen.
  class Only {
   public:
    Only(ConsoleOut& out, uint8_t mask) : out_(out), saved_(out.mask_) { out.mask_ = mask; }
    ~Only() { out_.mask_ = saved_; }

   private:
    ConsoleOut& out_;
    uint8_t saved_;
  };

 private:
  uint8_t mask_ = kBoth;
};

extern ConsoleOut Console;
