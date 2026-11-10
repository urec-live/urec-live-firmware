// Battery voltage of the Feather nRF52840 Express: the LiPo's VBAT goes through an on-board
// 1:2 resistor divider to A6 (PIN_VBAT). The divider is always connected (~14 uA, walkthrough §6).
#pragma once

#include <stdint.h>

// Battery voltage in mV, averaged over a few ADC samples (~0.2 ms in all). Restores the core's
// default ADC settings afterwards. On USB power the charger holds VBAT near 4.2 V, so the reading
// is the charger's, not the battery's (and with no battery plugged in it is meaningless).
uint16_t readBatteryMv();
