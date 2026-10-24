#include "lis3dh_motion.h"

namespace {
// Register map (LIS3DH datasheet, section 8)
const uint8_t kWhoAmI = 0x0F;      // reads 0x33
const uint8_t kTempCfg = 0x1F;
const uint8_t kCtrl1 = 0x20;
const uint8_t kCtrl2 = 0x21;
const uint8_t kCtrl3 = 0x22;
const uint8_t kCtrl4 = 0x23;
const uint8_t kCtrl5 = 0x24;
const uint8_t kCtrl6 = 0x25;
const uint8_t kStatus = 0x27;
const uint8_t kInt1Cfg = 0x30;
const uint8_t kInt1Src = 0x31;
const uint8_t kInt1Ths = 0x32;
const uint8_t kInt1Dur = 0x33;
const uint8_t kInt2Cfg = 0x34;
const uint8_t kClickCfg = 0x38;
const uint8_t kAutoIncrement = 0x80;  // MSB of the sub-address enables multi-byte reads

const uint8_t kStatusZyxda = 0x08;
const uint16_t kThsLsbMg = 16;   // +/-2 g
const int16_t kDataLsbMg = 16;   // low-power mode: 8-bit, 16 mg/digit at +/-2 g
}  // namespace

bool Lis3dhMotion::begin(TwoWire& wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
  if (read(kWhoAmI) != 0x33) return false;
  configureBase();
  return true;
}

void Lis3dhMotion::configureBase() {
  write(kInt1Cfg, 0x00);  // no interrupt while reconfiguring
  write(kCtrl1, 0x4F);    // ODR 50 Hz (0100), LPen=1, Z/Y/X enabled
  write(kCtrl2, 0x00);    // HP filter bypassed: gravity stays in the signal (the "direction")
  write(kCtrl3, 0x40);    // I1_IA1 only (clears the data-ready routing Adafruit's begin() sets)
  write(kCtrl4, 0x80);    // BDU=1, FS=+/-2 g, HR=0 (HR must be 0 in low-power mode)
  write(kCtrl5, 0x08);    // LIR_INT1: INT1 stays high until INT1_SRC is read
  write(kCtrl6, 0x00);    // active-high, nothing on INT2
  write(kTempCfg, 0x00);  // ADC/temperature off
  write(kInt2Cfg, 0x00);
  write(kClickCfg, 0x00);
  write(kInt1Ths, 0x00);
  write(kInt1Dur, 0x00);
  (void)read(kInt1Src);   // drop any latched event left over from before an MCU reset
  delay(2);               // low-power turn-on time is 1 ms
}

void Lis3dhMotion::arm(uint8_t axis, bool high, uint16_t thresholdMg, uint8_t duration) {
  uint16_t ths = static_cast<uint16_t>((thresholdMg + kThsLsbMg / 2) / kThsLsbMg);
  if (ths < 1) ths = 1;
  if (ths > 127) ths = 127;
  if (duration > 127) duration = 127;
  write(kInt1Cfg, 0x00);
  write(kInt1Ths, static_cast<uint8_t>(ths));
  write(kInt1Dur, duration);
  write(kInt1Cfg, eventBit(axis, high));  // AOI=0, 6D=0: OR of the one enabled event
}

void Lis3dhMotion::disarm() { write(kInt1Cfg, 0x00); }

uint8_t Lis3dhMotion::readInt1Source() { return read(kInt1Src); }

bool Lis3dhMotion::readSample(Sample* s, bool* fresh) {
  uint8_t buf[7];
  wire_->beginTransmission(address_);
  wire_->write(static_cast<uint8_t>(kStatus | kAutoIncrement));  // 0x27..0x2D, never touches 0x26
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom(address_, static_cast<uint8_t>(sizeof(buf))) != sizeof(buf)) return false;
  for (uint8_t i = 0; i < sizeof(buf); i++) buf[i] = static_cast<uint8_t>(wire_->read());
  if (fresh != nullptr) *fresh = (buf[0] & kStatusZyxda) != 0;
  for (uint8_t a = 0; a < 3; a++) {
    // Low-power data is the signed high byte; the low byte only has to be read for BDU.
    s->mg[a] = static_cast<int16_t>(static_cast<int8_t>(buf[2 + a * 2]) * kDataLsbMg);
  }
  return true;
}

bool Lis3dhMotion::waitSample(Sample* s, uint32_t timeoutMs) {
  const uint32_t start = millis();
  do {
    bool fresh = false;
    if (!readSample(s, &fresh)) return false;
    if (fresh) return true;
    delay(4);
  } while (millis() - start < timeoutMs);
  return false;
}

bool Lis3dhMotion::measure(uint8_t n, int16_t meanMg[3], int16_t p2pMg[3]) {
  Sample s;
  if (!waitSample(&s)) return false;  // discard: may straddle a configuration change
  int32_t sum[3] = {0, 0, 0};
  int16_t lo[3] = {INT16_MAX, INT16_MAX, INT16_MAX};
  int16_t hi[3] = {INT16_MIN, INT16_MIN, INT16_MIN};
  for (uint8_t i = 0; i < n; i++) {
    if (!waitSample(&s)) return false;
    for (uint8_t a = 0; a < 3; a++) {
      sum[a] += s.mg[a];
      if (s.mg[a] < lo[a]) lo[a] = s.mg[a];
      if (s.mg[a] > hi[a]) hi[a] = s.mg[a];
    }
  }
  for (uint8_t a = 0; a < 3; a++) {
    meanMg[a] = static_cast<int16_t>(sum[a] / n);
    p2pMg[a] = static_cast<int16_t>(hi[a] - lo[a]);
  }
  return true;
}

void Lis3dhMotion::write(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(value);
  wire_->endTransmission();
}

uint8_t Lis3dhMotion::read(uint8_t reg) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return 0;
  if (wire_->requestFrom(address_, static_cast<uint8_t>(1)) != 1) return 0;
  return static_cast<uint8_t>(wire_->read());
}
