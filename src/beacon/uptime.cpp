#include "uptime.h"

#include <Arduino.h>

#include "tick_clock.h"

uint32_t uptimeMs() {
  static TickType_t lastTicks = 0;
  static uint32_t wraps = 0;
  const TickType_t ticks = xTaskGetTickCount();
  if (ticks < lastTicks) wraps++;
  lastTicks = ticks;
  return gym::msFromTicks(wraps, ticks, configTICK_RATE_HZ);
}
