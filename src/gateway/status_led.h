// Onboard addressable RGB LED of the ESP32-C3-DevKitM-1 (GPIO8): short non-blocking flashes.
//   blue = beacon packet, green = upload OK, red = upload/network error
#pragma once

#include <Arduino.h>

class StatusLed {
 public:
  void flash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms = 40) {
    write(r, g, b);
    offAtMs_ = millis() + ms;
    on_ = true;
  }

  void update(uint32_t now) {
    if (on_ && static_cast<int32_t>(now - offAtMs_) >= 0) {
      write(0, 0, 0);
      on_ = false;
    }
  }

 private:
  static void write(uint8_t r, uint8_t g, uint8_t b) {
#ifdef RGB_BUILTIN
    rgbLedWrite(RGB_BUILTIN, r, g, b);
#endif
  }

  uint32_t offAtMs_ = 0;
  bool on_ = false;
};
