#include "console_out.h"

#include "ble_console.h"

ConsoleOut Console;

uint8_t ConsoleOut::listeners() const {
  uint8_t to = 0;
  if (Serial) to |= kUsb;  // a serial monitor has the USB port open
  if (ble_console::ready()) to |= kPhone;
  return to & mask_;
}

size_t ConsoleOut::write(const uint8_t* buffer, size_t size) {
  const uint8_t to = listeners();
  if (to & kUsb) Serial.write(buffer, size);
  if (to & kPhone) ble_console::write(buffer, size);
  return size;
}
