#include "ble_console.h"

#include <FreeRTOS.h>
#include <stdio.h>
#include <stream_buffer.h>

#include "beacon_config.h"
#include "gym_protocol.h"

namespace {

const size_t kTxBufferBytes = 4096;
const size_t kMaxChunk = 125;  // MTU 128 (BANDWIDTH_HIGH) minus the 3-byte ATT header
const uint32_t kSenderStackWords = 512;
const char kDroppedNotice[] = "\r\n[ble] console output dropped: too much at once\r\n";

BLEUart s_uart;
StreamBufferHandle_t s_tx = nullptr;
bool s_started = false;
#if BEACON_BLE_CONSOLE
size_t s_chunk = 20;  // bytes per notification: the MTU minus the ATT header
#endif

volatile uint32_t s_messages = 0;     // messages from the phone; written by the Bluefruit callback task
uint32_t s_seenMessages = 0;
volatile bool s_disconnected = false;  // written by the Bluefruit callback task
bool s_wasReady = false;
bool s_dropped = false;
uint32_t s_windowStartMs = 0;

bool elapsed(uint32_t now, uint32_t start, uint32_t duration) { return now - start >= duration; }

#if BEACON_BLE_CONSOLE
// Bluefruit callbacks: they run in its callback task, so they only leave a note for poll().
void onMessage(uint16_t) { s_messages = s_messages + 1; }
void onDisconnect(uint16_t, uint8_t) { s_disconnected = true; }

// Sends the queued console text. It runs at the loop task's priority and spends nearly all its time
// blocked, on the buffer or on a free notification slot; loop() runs while it waits.
void senderTask(void*) {
  uint8_t chunk[kMaxChunk];
  for (;;) {
    const size_t n = xStreamBufferReceive(s_tx, chunk, s_chunk, portMAX_DELAY);
    if (n > 0 && ble_console::ready()) s_uart.write(chunk, n);  // if the phone has left, it is dropped
  }
}
#endif

}  // namespace

namespace ble_console {

void configure() {
#if BEACON_BLE_CONSOLE
  // MTU 128 with short connection events (7.5 ms): one notification carries a console line, and
  // the link never holds the radio long enough to delay a rep burst.
  Bluefruit.configPrphBandwidth(BANDWIDTH_HIGH);
#endif
}

bool begin(uint32_t now) {
#if BEACON_BLE_CONSOLE
  static char name[32];
  snprintf(name, sizeof(name), "GetMeFit %s #%u", gym::equipmentName(BEACON_EQUIPMENT_TYPE),
           static_cast<unsigned>(BEACON_MACHINE_INSTANCE));
  Bluefruit.setName(name);
  Bluefruit.Periph.setConnIntervalMS(30, 60);  // console text needs little bandwidth
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);
  if (s_uart.begin() != ERROR_NONE) return false;
  s_uart.setRxCallback(onMessage);

  const uint16_t mtu = Bluefruit.getMaxMtu(BLE_GAP_ROLE_PERIPH);
  s_chunk = mtu > 3 ? mtu - 3 : 20;
  if (s_chunk > kMaxChunk) s_chunk = kMaxChunk;

  s_tx = xStreamBufferCreate(kTxBufferBytes, 1);
  if (s_tx == nullptr) return false;
  if (xTaskCreate(senderTask, "blecon", kSenderStackWords, nullptr, TASK_PRIO_LOW, nullptr) != pdPASS) return false;
  s_windowStartMs = now;
  s_started = true;
  return true;
#else
  (void)now;
  return false;
#endif
}

bool connected() { return s_started && Bluefruit.connected() > 0; }

bool ready() { return connected() && s_uart.notifyEnabled(); }

bool wantsAdvertising(uint32_t now) {
  if (!s_started || connected()) return false;
  return BEACON_BLE_CONSOLE_WINDOW_S == 0 ||
         !elapsed(now, s_windowStartMs, static_cast<uint32_t>(BEACON_BLE_CONSOLE_WINDOW_S) * 1000u);
}

BLEService& service() { return s_uart; }

size_t write(const uint8_t* data, size_t len) {
  if (s_tx == nullptr || len == 0) return 0;
  // All or nothing, so the phone never gets half a line.
  if (xStreamBufferSpacesAvailable(s_tx) < len) {
    s_dropped = true;
    return 0;
  }
  return xStreamBufferSend(s_tx, data, len, 0);
}

Stream& input() { return s_uart; }

bool takeMessageEnded() {
  const uint32_t m = s_messages;
  const bool ended = m != s_seenMessages;
  s_seenMessages = m;
  return ended;
}

bool poll(uint32_t now) {
  if (!s_started) return false;
  if (s_disconnected) {
    s_disconnected = false;
    s_windowStartMs = now;  // a new advertising window, so the phone can come back
    if (Serial) Serial.println(F("[ble] phone disconnected"));
  }
  if (s_dropped && xStreamBufferSpacesAvailable(s_tx) >= sizeof(kDroppedNotice) + 256) {
    s_dropped = false;
    write(reinterpret_cast<const uint8_t*>(kDroppedNotice), sizeof(kDroppedNotice) - 1);
  }
  const bool isReady = ready();
  const bool started = isReady && !s_wasReady;
  s_wasReady = isReady;
  if (started) {
    s_uart.flush();  // BLEUart::flush() empties the input: nothing typed before this counts
    s_seenMessages = s_messages;
    if (Serial) Serial.println(F("[ble] phone connected to the console"));
  }
  return started;
}

}  // namespace ble_console
