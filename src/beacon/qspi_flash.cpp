#include "qspi_flash.h"

#include "uptime.h"

#include <Arduino.h>
#include <nrfx_qspi.h>
#include <string.h>

namespace {

const uint8_t kCmdJedecId = 0x9F;
const uint8_t kCmdDeepPowerDown = 0xB9;
const uint8_t kCmdReleasePowerDown = 0xAB;
const uint32_t kWakeUs = 50;             // tRES1 is 20 us on the GD25Q16C
const uint32_t kWriteTimeoutMs = 50;     // a page program takes 0.6 ms (2.4 ms at most)
const uint32_t kSectorTimeoutMs = 1000;  // 4 KB erase: 45 ms (400 ms at most)
const uint32_t kBlockTimeoutMs = 3000;   // 64 KB erase: 150 ms (1.2 s at most)

// nrfx moves data by EasyDMA, which needs a word-aligned buffer in RAM.
alignas(4) uint8_t s_buf[QspiFlash::kPageBytes];

// Sends Release from Deep Power-down (0xAB) by hand, as plain SPI on the GPIOs, while the QSPI
// peripheral is off. A chip in deep power-down ignores every other command, and the peripheral's
// activation reads the chip's status first: from a sleeping chip that reads "busy" forever, and
// activating times out. So the chip is woken this way before every activation.
void wakeChipByHand() {
  pinMode(PIN_QSPI_IO2, OUTPUT);  // WP# high
  digitalWrite(PIN_QSPI_IO2, HIGH);
  pinMode(PIN_QSPI_IO3, OUTPUT);  // HOLD# high: low would pause the chip
  digitalWrite(PIN_QSPI_IO3, HIGH);
  pinMode(PIN_QSPI_CS, OUTPUT);
  digitalWrite(PIN_QSPI_CS, HIGH);
  pinMode(PIN_QSPI_SCK, OUTPUT);
  digitalWrite(PIN_QSPI_SCK, LOW);  // SPI mode 0
  pinMode(PIN_QSPI_IO0, OUTPUT);
  digitalWrite(PIN_QSPI_CS, LOW);
  for (int bit = 7; bit >= 0; bit--) {
    digitalWrite(PIN_QSPI_IO0, (kCmdReleasePowerDown >> bit) & 1 ? HIGH : LOW);
    digitalWrite(PIN_QSPI_SCK, HIGH);
    digitalWrite(PIN_QSPI_SCK, LOW);
  }
  digitalWrite(PIN_QSPI_CS, HIGH);
  delayMicroseconds(kWakeUs);
  // Hand the pins back as they were at reset; CS keeps a pull-up so the chip stays deselected.
  pinMode(PIN_QSPI_SCK, INPUT);
  pinMode(PIN_QSPI_IO0, INPUT);
  pinMode(PIN_QSPI_IO2, INPUT);
  pinMode(PIN_QSPI_IO3, INPUT);
  pinMode(PIN_QSPI_CS, INPUT_PULLUP);
}

}  // namespace

bool QspiFlash::command(uint8_t opcode, uint8_t length, void* rx) {
  nrf_qspi_cinstr_conf_t c = {};
  c.opcode = opcode;
  c.length = static_cast<nrf_qspi_cinstr_len_t>(length);
  c.io2_level = true;  // WP# and HOLD# high: with one data line they are the chip's control pins
  c.io3_level = true;
  c.wipwait = false;
  c.wren = false;
  return nrfx_qspi_cinstr_xfer(&c, nullptr, rx) == NRFX_SUCCESS;
}

bool QspiFlash::open() {
  if (open_) return true;
  nrfx_qspi_config_t cfg = {};
  cfg.xip_offset = 0;
  cfg.pins.sck_pin = g_ADigitalPinMap[PIN_QSPI_SCK];
  cfg.pins.csn_pin = g_ADigitalPinMap[PIN_QSPI_CS];
  cfg.pins.io0_pin = g_ADigitalPinMap[PIN_QSPI_IO0];
  cfg.pins.io1_pin = g_ADigitalPinMap[PIN_QSPI_IO1];
  cfg.pins.io2_pin = g_ADigitalPinMap[PIN_QSPI_IO2];
  cfg.pins.io3_pin = g_ADigitalPinMap[PIN_QSPI_IO3];
  cfg.prot_if.readoc = NRF_QSPI_READOC_FASTREAD;
  cfg.prot_if.writeoc = NRF_QSPI_WRITEOC_PP;
  cfg.prot_if.addrmode = NRF_QSPI_ADDRMODE_24BIT;
  cfg.prot_if.dpmconfig = false;
  cfg.phy_if.sck_delay = 10;  // 625 ns between chip selects, well above the chip's minimum
  cfg.phy_if.dpmen = false;
  cfg.phy_if.spi_mode = NRF_QSPI_MODE_0;
  cfg.phy_if.sck_freq = NRF_QSPI_FREQ_DIV2;  // 16 MHz
  cfg.irq_priority = 7;
  wakeChipByHand();  // harmless when it is awake
  nrfx_err_t err = nrfx_qspi_init(&cfg, nullptr, nullptr);
  if (err == NRFX_ERROR_INVALID_STATE) {  // left initialized by an earlier failure
    nrfx_qspi_uninit();
    err = nrfx_qspi_init(&cfg, nullptr, nullptr);
  }
  if (err != NRFX_SUCCESS) {
    // A timed-out activation leaves the driver initialized; undo that so the next open() can retry.
    if (err == NRFX_ERROR_TIMEOUT) nrfx_qspi_uninit();
    return false;
  }
  open_ = true;

  if (capacity_ == 0) {
    uint8_t id[3] = {0, 0, 0};
    if (command(kCmdJedecId, NRF_QSPI_CINSTR_LEN_4B, id) && id[0] != 0x00 && id[0] != 0xFF && id[2] >= 0x10 &&
        id[2] <= 0x18) {
      capacity_ = 1ul << id[2];  // GD25Q16C: C8 40 15 -> 2 MB
    }
  }
  if (capacity_ == 0) {
    close();
    return false;
  }
  return true;
}

void QspiFlash::close() {
  if (!open_) return;
  waitReady(kBlockTimeoutMs);
  command(kCmdDeepPowerDown, NRF_QSPI_CINSTR_LEN_1B, nullptr);
  // Also applies the nRF52840 anomaly 122 workaround (the QSPI drawing current once disabled),
  // inside nrf_qspi_disable().
  nrfx_qspi_uninit();
  pinMode(PIN_QSPI_CS, INPUT_PULLUP);  // keep the chip deselected while the peripheral is off
  open_ = false;
}

bool QspiFlash::waitReady(uint32_t timeoutMs) {
  const uint32_t start = uptimeMs();
  while (nrfx_qspi_mem_busy_check() != NRFX_SUCCESS) {
    if (uptimeMs() - start > timeoutMs) return false;
    yield();
  }
  return true;
}

bool QspiFlash::read(uint32_t addr, void* dst, size_t len) {
  if (!open_ || addr + len > capacity_) return false;
  uint8_t* out = static_cast<uint8_t*>(dst);
  while (len > 0) {
    const uint32_t aligned = addr & ~3ul;
    const size_t skip = addr - aligned;
    size_t chunk = len + skip;
    if (chunk > sizeof(s_buf)) chunk = sizeof(s_buf);
    const size_t span = (chunk + 3u) & ~3u;  // stays inside the chip: its size is a multiple of 4
    if (nrfx_qspi_read(s_buf, span, aligned) != NRFX_SUCCESS) return false;
    const size_t n = chunk - skip;
    memcpy(out, s_buf + skip, n);
    out += n;
    addr += n;
    len -= n;
  }
  return true;
}

bool QspiFlash::write(uint32_t addr, const void* src, size_t len) {
  if (!open_ || (addr & 3u) != 0 || (len & 3u) != 0 || addr + len > capacity_) return false;
  const uint8_t* in = static_cast<const uint8_t*>(src);
  while (len > 0) {
    size_t chunk = kPageBytes - (addr % kPageBytes);  // a page program must not cross a page
    if (chunk > len) chunk = len;
    memcpy(s_buf, in, chunk);
    if (!waitReady(kWriteTimeoutMs)) return false;
    if (nrfx_qspi_write(s_buf, chunk, addr) != NRFX_SUCCESS) return false;
    in += chunk;
    addr += chunk;
    len -= chunk;
  }
  return waitReady(kWriteTimeoutMs);  // so s_buf is free and the data is in the chip
}

bool QspiFlash::eraseSector(uint32_t addr) {
  if (!open_ || addr >= capacity_) return false;
  if (!waitReady(kSectorTimeoutMs)) return false;
  if (nrfx_qspi_erase(NRF_QSPI_ERASE_LEN_4KB, addr & ~(kSectorBytes - 1)) != NRFX_SUCCESS) return false;
  return waitReady(kSectorTimeoutMs);
}

bool QspiFlash::eraseBlock(uint32_t addr) {
  if (!open_ || addr >= capacity_) return false;
  if (!waitReady(kBlockTimeoutMs)) return false;
  if (nrfx_qspi_erase(NRF_QSPI_ERASE_LEN_64KB, addr & ~(kBlockBytes - 1)) != NRFX_SUCCESS) return false;
  return waitReady(kBlockTimeoutMs);
}

bool QspiFlash::isErased(uint32_t addr, size_t len) {
  uint8_t chunk[64];
  while (len > 0) {
    const size_t n = len < sizeof(chunk) ? len : sizeof(chunk);
    if (!read(addr, chunk, n)) return false;
    for (size_t i = 0; i < n; i++) {
      if (chunk[i] != 0xFF) return false;
    }
    addr += n;
    len -= n;
  }
  return true;
}
