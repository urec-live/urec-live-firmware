#include "tmag5273_mag.h"

#include <math.h>

namespace {
// Register map (TMAG5273 datasheet SLYS045C, section 8.1)
const uint8_t kDeviceConfig1 = 0x00;
const uint8_t kDeviceConfig2 = 0x01;
const uint8_t kSensorConfig1 = 0x02;
const uint8_t kSensorConfig2 = 0x03;
const uint8_t kTConfig = 0x07;
const uint8_t kIntConfig1 = 0x08;
const uint8_t kDeviceId = 0x0D;          // bits 1:0: 1 = A1 (+/-40/80 mT), 2 = A2 (+/-133/266 mT)
const uint8_t kManufacturerIdLsb = 0x0E; // 0x49, then 0x54 at 0x0F ("TI")
const uint8_t kTMsbResult = 0x10;        // T, X, Y, Z (MSB first), then CONV_STATUS at 0x18
const uint8_t kConvStatus = 0x18;
const uint8_t kDeviceStatus = 0x1C;      // latched error flags, write 1 to clear
// Bit 7 of the register byte is the conversion trigger, NOT an auto-increment flag: never OR it in
// for an ordinary read (the register pointer advances by itself on every byte).
const uint8_t kTriggerBit = 0x80;

const uint8_t kAvg32 = 0x14;           // CONV_AVG=101 (32x), MAG_TEMPCO=0 (no magnet), CRC off, standard reads
const uint8_t kStandbyLowNoise = 0x10; // LP_LN=1 (low noise), glitch filter on, trigger by I2C, standby
const uint8_t kSleepLowNoise = 0x11;   // same, sleep mode (5 nA)
const uint8_t kXyz = 0x70;             // MAG_CH_EN = X, Y, Z
const uint8_t kRangeLow = 0x00;        // +/-40 mT on X/Y and Z (A1)
const uint8_t kTempOn = 0x01;          // T_CH_EN: die temperature with every conversion
const uint8_t kMaskIntb = 0x01;        // INT pin off: it is not wired (and not on STEMMA QT)
const uint8_t kPorClear = 0x10;        // CONV_STATUS bit 4 (POR) is write-1-to-clear
const uint8_t kClearErrors = 0x0F;     // DEVICE_STATUS: OSC_ER, INT_ER, OTP_CRC_ER, VCC_UV_ER

const uint8_t kStatusPor = 0x10;
const uint32_t kWakeUs = 100;          // sleep -> standby takes 50 us (t_start_sleep)
const uint8_t kStatusDiag = 0x02;
// 32x on X, Y and Z: 70 us start-up + 825 + 2 * 800 us = 2495 us. The datasheet gives only typical
// values, and a result that completes while we read is lost, so wait with ~20 % margin.
const uint32_t kConversionUs = 3000;
const float kNoiseXyUt10 = 220.0f;     // 32x, low-noise: 22 uT rms on X/Y, 9 uT on Z (one conversion)
const float kNoiseZUt10 = 90.0f;
}  // namespace

bool Tmag5273Mag::begin(TwoWire& wire) {
  wire_ = &wire;
  wakeBus();  // it may still be asleep from before an MCU reset
  uint8_t id[2];
  if (!readRegs(kManufacturerIdLsb, id, 2) || id[0] != 0x49 || id[1] != 0x54) return false;
  uint8_t dev = 0;
  if (!readRegs(kDeviceId, &dev, 1)) return false;
  switch (dev & 0x03) {  // the upper bits are reserved and undefined
    case 1: rangeMt_ = 40; break;
    case 2: rangeMt_ = 133; break;
    default: return false;
  }
  if (!configure()) return false;
  sleep();
  return true;
}

// The whole configuration, DEVICE_CONFIG_2 (the operating mode) last. Also run after a power
// glitch (POR) or a diagnostic error, which the conversion status reports.
bool Tmag5273Mag::configure() {
  // The error flags latch: without clearing them a past supply dip would fail every conversion.
  const bool ok = write(kConvStatus, kPorClear) && write(kDeviceStatus, kClearErrors) &&
                  write(kDeviceConfig1, kAvg32) && write(kSensorConfig1, kXyz) &&
                  write(kSensorConfig2, kRangeLow) && write(kTConfig, kTempOn) &&
                  write(kIntConfig1, kMaskIntb) && write(kDeviceConfig2, kStandbyLowNoise);
  needsInit_ = !ok;
  lastSetCount_ = 0xFF;
  return ok;
}

// In sleep mode the chip wakes on its address but does not acknowledge it, so the first transfer
// after sleep() would fail. An address-only frame (NACK expected) wakes it, then it needs 50 us.
void Tmag5273Mag::wakeBus() {
  wire_->beginTransmission(address_);
  (void)wire_->endTransmission();
  delayMicroseconds(kWakeUs);  // not delay(): it counts 1/1024 s ticks and can return at once
}

bool Tmag5273Mag::wake() {
  if (wire_ == nullptr) return false;
  wakeBus();
  lastSetCount_ = 0xFF;  // sleep discards results
  return needsInit_ ? configure() : write(kDeviceConfig2, kStandbyLowNoise);
}

void Tmag5273Mag::sleep() {
  if (wire_ != nullptr) write(kDeviceConfig2, kSleepLowNoise);
}

// One triggered 32x conversion of T, X, Y and Z (raw codes).
bool Tmag5273Mag::convert(int32_t xyz[3], int32_t* tempCode) {
  // Trigger: rewrite DEVICE_CONFIG_2 (standby, low noise) with the trigger bit in the register byte.
  wire_->beginTransmission(address_);
  wire_->write(static_cast<uint8_t>(kDeviceConfig2 | kTriggerBit));
  wire_->write(kStandbyLowNoise);
  if (wire_->endTransmission() != 0) return false;
  // Wait without touching the bus: a result that completes while the device is addressed is
  // dropped. delayMicroseconds, because delay() counts 1/1024 s ticks and can return early.
  delayMicroseconds(kConversionUs);

  uint8_t buf[9];
  if (!readRegs(kTMsbResult, buf, sizeof(buf))) return false;
  const uint8_t status = buf[8];
  if (status & (kStatusPor | kStatusDiag)) {  // lost power or failed a self-check: set up again
    needsInit_ = true;
    return false;
  }
  const uint8_t setCount = status >> 5;
  if (setCount == lastSetCount_) return false;  // no new result
  lastSetCount_ = setCount;
  *tempCode = static_cast<int16_t>((buf[0] << 8) | buf[1]);
  for (uint8_t a = 0; a < 3; a++) xyz[a] = static_cast<int16_t>((buf[2 + a * 2] << 8) | buf[3 + a * 2]);
  return true;
}

bool Tmag5273Mag::read(int16_t b[3], int16_t* tempC10) {
  if (wire_ == nullptr) return false;
  if (needsInit_ && !configure()) return false;
  int32_t sum[3] = {0, 0, 0};
  int32_t tempSum = 0;
  uint8_t got = 0;
  for (uint8_t i = 0; i < oversample_; i++) {
    int32_t xyz[3];
    int32_t t;
    if (!convert(xyz, &t)) continue;
    for (uint8_t a = 0; a < 3; a++) sum[a] += xyz[a];
    tempSum += t;
    got++;
  }
  if (got == 0) return false;
  // B = code / 32768 * range; 1 mT = 10000 units of 0.1 uT.
  const float scale = rangeMt_ * 10000.0f / 32768.0f / got;
  for (uint8_t a = 0; a < 3; a++) {
    float v = sum[a] * scale;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32767.0f) v = -32767.0f;
    b[a] = static_cast<int16_t>(lroundf(v));
  }
  if (tempC10 != nullptr) {  // T = 25 + (code - 17508) / 58 degC (rev C)
    *tempC10 = static_cast<int16_t>(250 + lroundf((tempSum / static_cast<float>(got) - 17508.0f) * 10.0f / 58.0f));
  }
  return true;
}

void Tmag5273Mag::sampleNoise(int16_t out[3]) const {
  const float k = 1.0f / sqrtf(static_cast<float>(oversample_));
  out[0] = out[1] = static_cast<int16_t>(lroundf(kNoiseXyUt10 * k));
  out[2] = static_cast<int16_t>(lroundf(kNoiseZUt10 * k));
}

bool Tmag5273Mag::write(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool Tmag5273Mag::readRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
  wire_->beginTransmission(address_);
  wire_->write(reg);  // bit 7 clear: a plain register pointer
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom(address_, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = static_cast<uint8_t>(wire_->read());
  return true;
}
