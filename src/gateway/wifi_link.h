// Non-blocking Wi-Fi station: connects in the background and retries; never stalls the loop.
#pragma once

#include <stdint.h>

namespace wifi_link {

bool enabled();  // an SSID is configured
void begin();
void update(uint32_t nowMs);
bool connected();
bool takeJustConnected();  // true once after each (re)connection

}  // namespace wifi_link
