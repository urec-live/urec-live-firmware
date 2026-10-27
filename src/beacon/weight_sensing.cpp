#include "weight_sensing.h"

#include <InternalFileSystem.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "beacon_config.h"
#include "set_recorder.h"
#include "tmag5273_mag.h"
#include "weight_model.h"
#include "weight_pipeline.h"

using namespace Adafruit_LittleFS_Namespace;

#define LOG(...)                                \
  do {                                          \
    if (Serial) Serial.printf(__VA_ARGS__);     \
  } while (0)

static_assert(BEACON_MAG_SENSOR == MagSensor::Auto || BEACON_MAG_SENSOR == MagSensor::Tmag5273,
              "only the TMAG5273 is supported so far");
static_assert(BEACON_TMAG_OVERSAMPLE >= 1 && BEACON_TMAG_OVERSAMPLE <= 5,
              "BEACON_TMAG_OVERSAMPLE: 1..5 conversions (about 3.35 ms each) fit in one 20 ms sample");
// InternalFS is 28 KB and also holds Bluefruit's bonding data.
static_assert(gym::WeightModel::kMaxSerializedBytes <= 4096, "taught-weights file must stay under 4 KB");

namespace {
const uint32_t kSamplePeriodMs = 20;      // 50 Hz, as in the paper
const uint32_t kNotFreshRetryMs = 4;      // accelerometer had no new sample yet: look again soon
const uint8_t kMaxMagFails = 5;           // consecutive failed reads before the sensor counts as gone
const uint32_t kProbeIntervalMs = 30000;
const float kSegmentLowPassHz = 3.0f;
const int16_t kHeightFloorMm = 15;
const char* const kModelPath = "/w8_tmag.bin";

Tmag5273Mag s_tmag(BEACON_TMAG5273_ADDR, BEACON_TMAG_OVERSAMPLE);
Magnetometer* const s_mag = &s_tmag;
SetRecorder s_rec;
float s_aUp[kRecorderSamples];
float s_x[kRecorderSamples];
gym::SetData s_set = {};
gym::MotionAnalysis s_motion = {};
gym::RepFeatures s_feat[gym::kMaxRepSegments];
gym::WeightModel s_model;
uint8_t s_io[gym::WeightModel::kMaxSerializedBytes];

bool elapsed(uint32_t now, uint32_t start, uint32_t duration) { return now - start >= duration; }

// Labels are tenths of the unit: 500 -> "50", 125 -> "12.5".
void formatLabel(uint16_t tenths, char* out, size_t cap) {
  if (tenths % 10 == 0) {
    snprintf(out, cap, "%u", static_cast<unsigned>(tenths / 10));
  } else {
    snprintf(out, cap, "%u.%u", static_cast<unsigned>(tenths / 10), static_cast<unsigned>(tenths % 10));
  }
}

bool parseLabel(const char* text, uint16_t* out) {
  if (text == nullptr || *text == '\0') return false;
  char* end = nullptr;
  const float w = strtof(text, &end);
  if (*end != '\0' || !(w > 0.0f) || w > 6000.0f) return false;
  *out = static_cast<uint16_t>(lroundf(w * 10.0f));
  return *out > 0;
}

// Classifier scale floors: the noise left in each feature after averaging (zenith: 11 samples,
// 5 cm point: 5 samples), so a feature is never trusted beyond what the sensor can resolve.
void modelFloors(const Magnetometer& m, int16_t floors[gym::kWeightFeatureCount]) {
  int16_t noise[3];
  m.sampleNoise(noise);
  for (uint8_t a = 0; a < 3; a++) {
    floors[gym::kFeatZenithX + a] = static_cast<int16_t>(lroundf(fmaxf(3.0f, noise[a] / sqrtf(11.0f))));
    floors[gym::kFeatLift5X + a] = static_cast<int16_t>(lroundf(fmaxf(3.0f, noise[a] / sqrtf(5.0f))));
  }
  floors[gym::kFeatHeight] = kHeightFloorMm;
}

uint16_t medianLiftMm(const gym::MotionAnalysis& m) {
  uint16_t h[gym::kMaxRepSegments];
  size_t n = 0;
  for (size_t k = 0; k < m.segCount; k++) {
    if (m.seg[k].valid) h[n++] = m.seg[k].heightMm;
  }
  if (n == 0) return 0;
  for (size_t i = 1; i < n; i++) {  // insertion sort: n is small
    const uint16_t v = h[i];
    size_t j = i;
    while (j > 0 && h[j - 1] > v) {
      h[j] = h[j - 1];
      j--;
    }
    h[j] = v;
  }
  return h[n / 2];
}
}  // namespace

// ---------------------------------------------------------------- sensor

void WeightSensing::begin(TwoWire& wire, Lis3dhMotion& imu, bool flashOk, uint32_t now) {
  wire_ = &wire;
  imu_ = &imu;
  flashOk_ = flashOk;
  if (flashOk_) InternalFS.begin();  // already mounted by Bluefruit for bonding; harmless twice
  probe(now);
  if (!present_) LOG("[weight] no magnetometer at 0x%02X - weight sensing off\n", BEACON_TMAG5273_ADDR);
}

void WeightSensing::housekeeping(uint32_t now) {
  if (present_ || inSet_ || !elapsed(now, lastProbeMs_, kProbeIntervalMs)) return;
  probe(now);
}

void WeightSensing::probe(uint32_t now) {
  lastProbeMs_ = now;
  if (!s_mag->begin(*wire_)) return;  // also leaves it asleep
  present_ = true;
  magAwake_ = false;
  int16_t floors[gym::kWeightFeatureCount];
  modelFloors(*s_mag, floors);
  s_model.setFloors(floors);
  if (!modelLoaded_) loadModel();
  uint16_t ls[gym::WeightModel::kMaxLabels];
  uint16_t cs[gym::WeightModel::kMaxLabels];
  LOG("[weight] %s at 0x%02X, %u weights taught\n", s_mag->name(), s_mag->address(),
      static_cast<unsigned>(s_model.labels(ls, cs, gym::WeightModel::kMaxLabels)));
  updatePower(now);  // the stream may already be on
}

// The magnetometer is awake exactly while the sampler needs it; the sampler runs while a set is
// recorded or the stream is on.
void WeightSensing::updatePower(uint32_t now) {
  const bool wantMag = present_ && (recording_ || streaming_);
  if (wantMag && !magAwake_) {
    if (s_mag->wake()) {
      magAwake_ = true;
      magFails_ = 0;
    } else {
      LOG("[weight] %s does not answer - weight sensing off until it is back\n", s_mag->name());
      present_ = false;
      magLost_ = true;
      recording_ = false;
    }
  } else if (!wantMag && magAwake_) {
    s_mag->sleep();
    magAwake_ = false;
  }
  const bool sample = recording_ || streaming_;
  if (sample && !samplerOn_) nextSampleMs_ = now;
  samplerOn_ = sample;
}

void WeightSensing::setStreaming(bool on, uint32_t now) {
  if (on == streaming_) return;
  streaming_ = on;
  updatePower(now);
}

bool WeightSensing::tick(uint32_t now) {
  if (!samplerOn_ || static_cast<int32_t>(now - nextSampleMs_) < 0) return false;
  Lis3dhMotion::Sample s;
  bool fresh = false;
  if (!imu_->readSample(&s, &fresh) || !fresh) {
    nextSampleMs_ = now + kNotFreshRetryMs;  // a gap shows up in the timestamps, never a repeat
    return false;
  }
  // Keep a steady 50 Hz: step by one period, or resync after a long gap.
  nextSampleMs_ = (now - nextSampleMs_ < kSamplePeriodMs) ? nextSampleMs_ + kSamplePeriodMs
                                                          : now + kSamplePeriodMs;
  int16_t b[kMaxMagnetometers][3] = {};
  uint8_t valid = 0;
  if (magAwake_) {
    int16_t temp = 0;
    if (s_mag->read(b[0], &temp)) {
      valid |= 1;
      magFails_ = 0;
      lastTempC10_ = temp;
    } else if (++magFails_ >= kMaxMagFails) {
      LOG("[weight] %s stopped answering - this set is not weighed\n", s_mag->name());
      present_ = false;
      magAwake_ = false;
      magLost_ = true;
      recording_ = false;
      updatePower(now);
    }
  }
  if (recording_) s_rec.add(now, s.mg, b, valid);
  memcpy(latest_.accMg, s.mg, sizeof(latest_.accMg));
  memcpy(latest_.b, b[0], sizeof(latest_.b));
  latest_.bValid = (valid & 1) != 0;
  return true;
}

// ---------------------------------------------------------------- set lifecycle

void WeightSensing::startSet(uint32_t now, const char* blocked) {
  if (inSet_) return;
  inSet_ = true;
  blocked_ = blocked;
  magLost_ = false;
  if (dumping()) {
    dumpSection_ = kDumpIdle;
    LOG("# dump aborted: a new set started\n");
  }
  recording_ = present_ && blocked == nullptr;
  if (recording_) {
    s_rec.clear(now);
    rawValid_ = false;  // the last set's samples are being overwritten
    recordStartMs_ = now;
  }
  updatePower(now);
}

void WeightSensing::onCount(uint32_t now) {
  if (recording_) s_rec.addCount(now);
}

void WeightSensing::abortSet(uint32_t now) {
  if (!inSet_) return;
  inSet_ = false;
  lastFeatN_ = 0;  // `teach` must not label an earlier set with this one's weight
  if (recording_) LOG("[weight] set aborted (cal / reset / mode change): not weighed\n");
  recording_ = false;
  updatePower(now);
}

void WeightSensing::finishSet(uint32_t now, uint32_t lastEventMs, uint8_t setSeq, uint16_t countedReps,
                              uint16_t minRepIntervalMs) {
  if (!inSet_) return;
  inSet_ = false;
  const bool recorded = recording_;
  recording_ = false;
  updatePower(now);
  // Whatever happens below, `teach` now refers to this set: it must not label an earlier one.
  lastEndedSeq_ = setSeq;
  lastFeatN_ = 0;
  lastTaught_ = false;
  if (!present_ && !magLost_) return;  // no magnetometer: nothing to say
  if (blocked_ != nullptr) {
    LOG("[weight] set %u not weighed: %s\n", setSeq, blocked_);
    return;
  }
  if (!recorded || magLost_) {
    LOG("[weight] set %u not weighed: the magnetometer dropped out\n", setSeq);
    return;
  }

  s_rec.finish(lastEventMs, &s_set);
  haveSet_ = true;
  rawValid_ = true;
  lastSetSeq_ = setSeq;
  lastCounted_ = countedReps;
  haveRest_ = false;
  if (s_rec.wrapped()) LOG("[weight] set %u is longer than %u s: using its last %u s\n", setSeq,
                           BEACON_W8_MAX_SET_S, BEACON_W8_MAX_SET_S);

  gym::WeightPipelineConfig cfg;
  cfg.minRepMs = BEACON_W8_MIN_REP_MS;
  cfg.minRepIntervalMs = minRepIntervalMs;
  cfg.stillP2pMg = BEACON_STILL_P2P_MG;
  cfg.lowPassHz = kSegmentLowPassHz;
  lastError_ = nullptr;
  if (!gym::analyzeMotion(s_set, cfg, s_aUp, s_x, &s_motion)) {
    lastError_ = s_motion.error;
  } else {
    int16_t noise[3];
    s_mag->sampleNoise(noise);
    const gym::MagData md = s_rec.magData(0, noise);
    lastFeatN_ = gym::magFeatures(s_set, md, s_motion, s_feat, gym::kMaxRepSegments, lastRest_, &lastError_);
    haveRest_ = lastFeatN_ > 0;
  }
  if (logAll_ && Serial) startDump();
  if (lastFeatN_ == 0) {
    LOG("[weight] set %u not weighed: %s\n", setSeq, lastError_ != nullptr ? lastError_ : "no reps");
    return;
  }

  char lift[24];
  snprintf(lift, sizeof(lift), "lift %u cm", (medianLiftMm(s_motion) + 5) / 10);
  char dT[24] = "";
  if (s_motion.adapted) snprintf(dT, sizeof(dT), ", dT %u ms", s_motion.minRepMsUsed);
  if (s_model.size() == 0) {
    LOG("[weight] set %u: %u reps measured, %s%s - no weights taught yet: type \"teach <weight>\"\n",
        setSeq, static_cast<unsigned>(lastFeatN_), lift, dT);
    return;
  }
  const gym::WeightEstimate e = s_model.classify(s_feat, lastFeatN_);
  char label[12];
  formatLabel(e.label, label, sizeof(label));
  if (e.decided) {
    LOG("[weight] set %u: ~%s (%u/%u reps agree) | %s, %s%s\n", setSeq, label, e.agree, e.votes,
        s_mag->name(), lift, dT);
  } else if (e.agree > 0) {
    LOG("[weight] set %u: uncertain - %u/%u reps say %s, %u unlike any taught weight | %s, %s%s\n", setSeq,
        e.agree, e.votes, label, e.unknown, s_mag->name(), lift, dT);
  } else {
    LOG("[weight] set %u: unknown - none of the %u reps looks like a taught weight | %s, %s%s\n", setSeq,
        e.votes, s_mag->name(), lift, dT);
  }
}

// ---------------------------------------------------------------- persistence

void WeightSensing::loadModel() {
  modelLoaded_ = true;
  if (!flashOk_) return;
  File f = InternalFS.open(kModelPath, FILE_O_READ);
  if (!f) return;
  const int n = f.read(s_io, sizeof(s_io));
  f.close();
  if (n <= 0 || !s_model.deserialize(s_mag->sensorId(), s_io, static_cast<size_t>(n))) {
    LOG("[weight] %s is from another firmware version - ignored (teach again)\n", kModelPath);
  }
}

bool WeightSensing::saveModel() {
  if (!flashOk_) return false;
  InternalFS.remove(kModelPath);  // FILE_O_WRITE appends, so start from an empty file
  if (s_model.size() == 0) return true;
  const size_t n = s_model.serialize(s_mag->sensorId(), s_io, sizeof(s_io));
  File f = InternalFS.open(kModelPath, FILE_O_WRITE);
  if (!f) return false;
  const size_t written = f.write(s_io, n);
  f.close();
  return written == n;
}

// ---------------------------------------------------------------- console

void WeightSensing::printHelp() const {
  Serial.println(F("  mag                  magnetometer, field at rest, taught weights"));
  Serial.println(F("  teach <w>            label the last set with weight w (teach 50, teach 12.5)"));
  Serial.println(F("  weights              list the taught weights"));
  Serial.println(F("  forget <w>|all       delete a taught weight"));
  Serial.println(F("  dump                 print the last set as CSV (offline training)"));
  Serial.println(F("  log                  toggle: dump every set automatically"));
}

void WeightSensing::printSummary() const {
  if (!present_) {
    Serial.println(F("weight: no magnetometer (looking again every 30 s)"));
    return;
  }
  uint16_t ls[gym::WeightModel::kMaxLabels];
  uint16_t cs[gym::WeightModel::kMaxLabels];
  const size_t k = s_model.labels(ls, cs, gym::WeightModel::kMaxLabels);
  Serial.printf("weight: %s at 0x%02X, %u weights taught (%u reps)%s\n", s_mag->name(), s_mag->address(),
                static_cast<unsigned>(k), static_cast<unsigned>(s_model.size()), recording_ ? ", recording" : "");
}

void WeightSensing::printWeights() const {
  uint16_t ls[gym::WeightModel::kMaxLabels];
  uint16_t cs[gym::WeightModel::kMaxLabels];
  const size_t k = s_model.labels(ls, cs, gym::WeightModel::kMaxLabels);
  if (k == 0) {
    Serial.println(F("weights: none taught (do a set, then: teach <weight>)"));
    return;
  }
  Serial.print(F("weights:"));
  for (size_t i = 0; i < k; i++) {
    char label[12];
    formatLabel(ls[i], label, sizeof(label));
    Serial.printf(" %s (%u reps)%s", label, cs[i], i + 1 < k ? "," : "\n");
  }
}

void WeightSensing::printStatus() const {
  if (!present_) {
    Serial.printf("magnetometer: none at 0x%02X (looking again every 30 s)\n", BEACON_TMAG5273_ADDR);
  } else {
    Serial.printf("magnetometer: %s at 0x%02X, %s, %.1f C\n", s_mag->name(), s_mag->address(),
                  magAwake_ ? "sampling" : "asleep", lastTempC10_ / 10.0f);
  }
  if (haveRest_) {
    Serial.printf("  field at rest after set %u: x %.1f, y %.1f, z %.1f uT\n", lastSetSeq_, lastRest_[0] / 10.0f,
                  lastRest_[1] / 10.0f, lastRest_[2] / 10.0f);
  }
  if (recording_) {
    Serial.printf("  recording: %.1f s, %u samples\n", (millis() - recordStartMs_) / 1000.0f,
                  static_cast<unsigned>(s_rec.size()));
  } else if (inSet_ && blocked_ != nullptr) {
    Serial.printf("  this set is not weighed: %s\n", blocked_);
  }
  if (haveSet_ && !lastTaught_ && lastFeatN_ > 0) {
    Serial.printf("  last set %u: %u reps ready to teach\n", lastSetSeq_, static_cast<unsigned>(lastFeatN_));
  }
  Serial.printf("  auto-dump %s, taught weights %s\n", logAll_ ? "on" : "off",
                flashOk_ ? "saved in flash" : "NOT saved (Bluetooth stack did not start)");
  printWeights();
}

bool WeightSensing::command(const char* cmd, const char* arg) {
  if (strcmp(cmd, "mag") == 0) {
    printStatus();
  } else if (strcmp(cmd, "weights") == 0) {
    printWeights();
  } else if (strcmp(cmd, "teach") == 0) {
    uint16_t label = 0;
    if (!parseLabel(arg, &label)) {
      Serial.println(F("usage: teach <weight>   e.g. teach 50 or teach 12.5"));
    } else if (lastFeatN_ == 0) {
      if (lastEndedSeq_ != 0) {
        Serial.printf("set %u was not weighed - nothing to teach (do another set)\n", lastEndedSeq_);
      } else {
        Serial.println(F("no weighed set to teach from - do a set first"));
      }
    } else if (lastTaught_) {
      Serial.println(F("that set is already taught - do another set"));
    } else {
      const size_t added = s_model.add(label, s_feat, lastFeatN_);
      char text[12];
      formatLabel(label, text, sizeof(text));
      if (added == 0) {
        Serial.printf("too many different weights (%u) - forget one first\n",
                      static_cast<unsigned>(gym::WeightModel::kMaxLabels));
      } else {
        lastTaught_ = true;
        const bool saved = saveModel();
        Serial.printf("taught %s: %u reps of set %u (%u reps in total)%s\n", text, static_cast<unsigned>(added),
                      lastSetSeq_, static_cast<unsigned>(s_model.size()), saved ? "" : " - NOT saved to flash");
      }
    }
  } else if (strcmp(cmd, "forget") == 0) {
    uint16_t label = 0;
    if (arg != nullptr && strcmp(arg, "all") == 0) {
      s_model.clear();
      saveModel();
      Serial.println(F("all taught weights deleted"));
    } else if (parseLabel(arg, &label)) {
      const size_t removed = s_model.forget(label);
      saveModel();
      Serial.printf("deleted %u reps\n", static_cast<unsigned>(removed));
    } else {
      Serial.println(F("usage: forget <weight>|all"));
    }
  } else if (strcmp(cmd, "dump") == 0) {
    if (!haveSet_) {
      Serial.println(F("no recorded set yet"));
    } else if (inSet_ && recording_) {
      Serial.println(F("a set is being recorded - dump after it ends"));
    } else if (!rawValid_) {
      Serial.println(F("a later set that was not weighed overwrote the samples - nothing to dump"));
    } else {
      startDump();
    }
  } else if (strcmp(cmd, "log") == 0) {
    logAll_ = !logAll_;
    Serial.printf("auto-dump %s\n", logAll_ ? "on" : "off");
  } else {
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- CSV dump

// The dump is written a few lines at a time from loop(), only while the USB buffer has room, so a
// slow (or stuck) serial monitor never delays the rep counting.
void WeightSensing::startDump() {
  dumpSection_ = kDumpSummary;
  dumpIndex_ = 0;
}

void WeightSensing::pumpDump() {
  if (!dumping()) return;
  if (!Serial) {  // the monitor went away
    dumpSection_ = kDumpIdle;
    return;
  }
  for (uint8_t lines = 0; lines < 32 && dumping() && Serial.availableForWrite() >= 128; lines++) {
    char line[128];
    if (nextDumpLine(line, sizeof(line))) Serial.print(line);
  }
}

// Produces the next dump line (with its newline); false when a section ends without a line.
bool WeightSensing::nextDumpLine(char* line, size_t cap) {
  const size_t kPerLine = 10;
  switch (dumpSection_) {
    case kDumpSummary:
      snprintf(line, cap, "# w8 set=%u reps=%u samples=%u sensor=%s dT_ms=%u adapted=%u\n", lastSetSeq_,
               lastCounted_, static_cast<unsigned>(s_set.n), s_mag->name(), s_motion.minRepMsUsed,
               s_motion.adapted ? 1 : 0);
      dumpSection_ = kDumpError;
      return true;
    case kDumpError:  // own line: the longest message would not fit after the summary
      snprintf(line, cap, "# error=%s\n", lastError_ != nullptr ? lastError_ : "none");
      dumpSection_ = kDumpRest;
      return true;
    case kDumpRest:
      if (s_motion.restEnd == 0) {  // the set was rejected before a rest window was found
        snprintf(line, cap, "# rest=none temp_c=%.1f\n", lastTempC10_ / 10.0f);
        dumpSection_ = kDumpCounts;
        dumpIndex_ = 0;
        return true;
      }
      snprintf(line, cap, "# rest=%u..%u gravity_mg=%.1f,%.1f,%.1f b_rest_0.1uT=%d,%d,%d temp_c=%.1f\n",
               s_motion.restStart, s_motion.restEnd, s_motion.gravityMg[0], s_motion.gravityMg[1],
               s_motion.gravityMg[2], lastRest_[0], lastRest_[1], lastRest_[2], lastTempC10_ / 10.0f);
      dumpSection_ = kDumpCounts;
      dumpIndex_ = 0;
      return true;
    case kDumpCounts:
    case kDumpValleys: {
      const bool counts = dumpSection_ == kDumpCounts;
      const size_t total = counts ? s_set.countN : s_motion.valleyCount;
      if (dumpIndex_ >= total) {
        dumpSection_ = counts ? kDumpValleys : kDumpSegments;
        dumpIndex_ = 0;
        return false;
      }
      size_t len = snprintf(line, cap, counts ? "# counts_ms=" : "# valleys=");
      for (size_t k = 0; k < kPerLine && dumpIndex_ < total && len + 8 < cap; k++, dumpIndex_++) {
        const unsigned v = counts ? s_set.countMs[dumpIndex_] : s_motion.valleys[dumpIndex_];
        len += snprintf(line + len, cap - len, k == 0 ? "%u" : ",%u", v);
      }
      snprintf(line + len, cap - len, "\n");
      return true;
    }
    case kDumpSegments: {
      if (dumpIndex_ >= s_motion.segCount) {
        dumpSection_ = kDumpColumns;
        return false;
      }
      const gym::RepSegment& r = s_motion.seg[dumpIndex_];
      snprintf(line, cap, "# segment=%u start=%u end=%u bottom=%u zenith=%u lift5=%u height_mm=%u valid=%u\n",
               static_cast<unsigned>(dumpIndex_), r.start, r.end, r.bottom, r.zenith, r.lift5, r.heightMm,
               r.valid ? 1 : 0);
      dumpIndex_++;
      return true;
    }
    case kDumpColumns:
      snprintf(line, cap, "t_ms,ax_mg,ay_mg,az_mg,bx_0.1uT,by_0.1uT,bz_0.1uT,valid\n");
      dumpSection_ = kDumpRows;
      dumpIndex_ = 0;
      return true;
    case kDumpRows: {
      if (dumpIndex_ >= s_set.n) {
        dumpSection_ = kDumpEnd;
        return false;
      }
      const size_t i = dumpIndex_++;
      const int16_t* a = s_rec.acc(i);
      const int16_t* b = s_rec.mag(0, i);
      snprintf(line, cap, "%u,%d,%d,%d,%d,%d,%d,%u\n", s_rec.timeMs(i), a[0], a[1], a[2], b[0], b[1], b[2],
               s_rec.flags(i) & 1);
      return true;
    }
    case kDumpEnd:
      snprintf(line, cap, "# end\n");
      dumpSection_ = kDumpIdle;
      return true;
    case kDumpIdle:
    default:
      return false;
  }
}
