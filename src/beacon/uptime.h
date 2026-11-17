// Milliseconds since power-up for every timer in the beacon: like millis(), but it wraps at 2^32 as
// "now - start >= duration" assumes, where the core's millis() jumps back to 0 after 48.5 days
// (lib/gym_core/src/tick_clock.h). Without it, every running timer would fire at once at that
// moment, and a set in progress would be cut in two.
//
// It keeps the last tick count without a lock: call it from the loop task only (all of src/beacon
// runs there), which also calls it far more often than once per 48 days.
#pragma once

#include <stdint.h>

uint32_t uptimeMs();
