// Milliseconds from a 32-bit tick counter, wrapping at 2^32 ms like an unsigned timer must
// (pure arithmetic, unit-tested; src/beacon/uptime.h is the FreeRTOS side).
//
// The Adafruit nRF52 core's millis() is ticks * 1000 / 1024 of the 32-bit FreeRTOS tick count, so it
// jumps from 4,194,303,999 back to 0 after 48.5 days, where every "now - start >= duration" timer
// misfires. Counting the tick counter's wraps and converting all 64 bits avoids the jump.
#pragma once

#include <stdint.h>

namespace gym {

// `wraps` times the 32-bit tick counter has wrapped, plus `ticks`, at `hz` ticks per second, in ms
// modulo 2^32.
inline uint32_t msFromTicks(uint32_t wraps, uint32_t ticks, uint32_t hz) {
  const uint64_t total = (static_cast<uint64_t>(wraps) << 32) | ticks;
  return static_cast<uint32_t>(total * 1000u / hz);
}

}  // namespace gym
