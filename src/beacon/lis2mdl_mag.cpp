#include "lis2mdl_mag.h"

namespace {
// Register map (LIS2MDL datasheet DocID030621, section 8)
const uint8_t kWhoAmI = 0x4F;  // reads 0x40
const uint8_t kCfgRegA = 0x60;
const uint8_t kCfgRegB = 0x61;
const uint8_t kCfgRegC = 0x62;
const uint8_t kStatus = 0x67;  // bit 3 ZYXDA: a new X/Y/Z sample since the last read
const uint8_t kOutXL = 0x68;   // X, Y, Z little-endian, then TEMP_OUT at 0x6E
const uint8_t kAutoIncrement = 0x80;  // MSB of the sub-address: multi-byte reads advance
const uint8_t kZyxda = 0x08;

const uint8_t kSoftReset = 0x20;      // CFG_REG_A SOFT_RST (clears itself)
const uint8_t kContinuous50Hz = 0x88; // COMP_TEMP_EN (must be 1), high resolution, ODR 50 Hz, continuous
const uint8_t kIdle = 0x83;           // COMP_TEMP_EN, idle mode (1.5 uA)
const uint8_t kOffsetCancel = 0x02;   // CFG_REG_B OFF_CANC: set/reset every sample, 0.3 uT rms noise
const uint8_t kBdu = 0x10;            // CFG_REG_C BDU; never set bit 5 (I2C_DIS) or bit 2 (4WSPI)

const uint32_t kSettleMs = 30;        // high-resolution turn-on 9.4 ms + one 20 ms sample
const int16_t kNoiseUt10 = 3;         // 0.3 uT rms with offset cancellation
}  // namespace

bool Lis2mdlMag::begin(TwoWire& wire) {
  wire_ = &wire;
  uint8_t id = 0;
  if (!readRegs(kWhoAmI, &id, 1) || id != 0x40) return false;
  return configure();
}

// Soft reset (an MCU reset does not power-cycle the breakout), then the configuration with the
// operating mode last. Leaves the sensor idle. The OFFSET registers stay 0: the field the steel
// bends is the signal, and the per-set rest field is subtracted in software.
bool Lis2mdlMag::configure() {
  if (!write(kCfgRegA, kSoftReset)) return false;
  for (uint8_t i = 0; i < 10; i++) {
    delay(1);
    uint8_t a = kSoftReset;
    if (readRegs(kCfgRegA, &a, 1) && (a & kSoftReset) == 0) break;
  }
  return write(kCfgRegC, kBdu) && write(kCfgRegB, kOffsetCancel) && write(kCfgRegA, kIdle);
}

// The whole configuration again (about 1-2 ms, once per set): the breakout may have lost power on
// its own since the last set, which resets the chip to idle with BDU and offset cancellation off.
bool Lis2mdlMag::wake() {
  if (wire_ == nullptr || !configure() || !write(kCfgRegA, kContinuous50Hz)) return false;
  readyAtMs_ = millis() + kSettleMs;  // no blocking: read() reports "no sample" until then
  return true;
}

void Lis2mdlMag::sleep() {
  if (wire_ != nullptr) write(kCfgRegA, kIdle);
}

// One burst of CFG_REG_A (0x60) to TEMP_OUT_H (0x6F): the configuration, the status, the field and
// the temperature, all from the same moment.
bool Lis2mdlMag::read(int16_t b[3], int16_t* tempC10) {
  if (wire_ == nullptr || static_cast<int32_t>(millis() - readyAtMs_) < 0) return false;
  uint8_t r[16];
  if (!readRegs(kCfgRegA, r, sizeof(r))) return false;
  if (r[0] != kContinuous50Hz || (r[kCfgRegC - kCfgRegA] & kBdu) == 0) {
    wake();  // lost power mid-set: set it up again; the next ~30 ms report no sample
    return false;
  }
  if ((r[kStatus - kCfgRegA] & kZyxda) == 0) return false;  // no new sample since the last read
  const uint8_t* out = &r[kOutXL - kCfgRegA];
  for (uint8_t a = 0; a < 3; a++) {
    const int16_t raw = static_cast<int16_t>(out[a * 2] | (out[a * 2 + 1] << 8));
    // 1.5 mG = 0.15 uT per step; in units of 0.1 uT that is * 3 / 2.
    int32_t v = static_cast<int32_t>(raw) * 3 / 2;
    if (v > 32767) v = 32767;
    if (v < -32767) v = -32767;
    b[a] = static_cast<int16_t>(v);
  }
  if (tempC10 != nullptr) {  // TEMP_OUT, 8 LSB per degC around 25 degC
    *tempC10 = static_cast<int16_t>(250 + static_cast<int16_t>(out[6] | (out[7] << 8)) * 10 / 8);
  }
  return true;
}

void Lis2mdlMag::sampleNoise(int16_t out[3]) const {
  out[0] = out[1] = out[2] = kNoiseUt10;
}

bool Lis2mdlMag::write(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool Lis2mdlMag::readRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
  wire_->beginTransmission(address_);
  wire_->write(static_cast<uint8_t>(n > 1 ? (reg | kAutoIncrement) : reg));
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom(address_, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = static_cast<uint8_t>(wire_->read());
  return true;
}
