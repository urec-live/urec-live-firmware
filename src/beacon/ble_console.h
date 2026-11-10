// Bluetooth console for a phone: the USB tuning console's commands and replies over the Nordic UART
// Service, which Adafruit's Bluefruit Connect app (UART), nRF Connect and Serial Bluetooth Terminal
// all speak. With it the beacon can be checked, taught and read with no cable to a laptop.
//
// The beacon has one advertising set, shared with the rep bursts (ibeacon_adv.h). It advertises as
// connectable, under the name "GetMeFit <machine> #<n>", only while no phone is connected, no burst
// is on the air and the advertising window is open: BEACON_BLE_CONSOLE_WINDOW_S after power-up and
// after a phone disconnects. The rep and status bursts go on while a phone is connected, so the
// gateway keeps counting.
//
// Output never blocks the caller. Text goes into a 4 KB buffer that a separate task sends as
// notifications, so a slow phone cannot hold up the rep counting or the 50 Hz weight sampler. Text
// that does not fit is dropped, and the phone is told. CSV dumps and the Teleplot stream are too
// long for this link and stay on USB (console_out.h).
#pragma once

#include <Arduino.h>
#include <bluefruit.h>

namespace ble_console {

// Before Bluefruit.begin(): connection buffers sized for console text.
void configure();

// After Bluefruit.begin(): the UART service, the device name and the sending task. Opens the first
// advertising window at `now`.
bool begin(uint32_t now);

// A phone is connected and listening to the console output.
bool ready();

// A phone is connected (it may not be listening yet).
bool connected();

// The beacon should advertise for a phone: the console runs, no phone is connected and the
// advertising window is open.
bool wantsAdvertising(uint32_t now);

// The UART service, for the advertising data.
BLEService& service();

// Queues console output for the phone. Never blocks; returns how many bytes were queued.
size_t write(const uint8_t* data, size_t len);

// Bytes typed on the phone.
Stream& input();

// True once after each message from the phone (an app's Send). Call it BEFORE reading input(): a
// message whose bytes are read in the same pass then ends its line even without a newline.
bool takeMessageEnded();

// Connection bookkeeping, from loop(). Returns true when a phone has just started listening.
bool poll(uint32_t now);

// One line for the `s` status: a phone connected, or how long the beacon still advertises.
void printStatus(Print& out, uint32_t now);

}  // namespace ble_console
