#include "weight_sensing.h"

#include <InternalFileSystem.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "beacon_config.h"
#include "lis2mdl_mag.h"
#include "set_recorder.h"
#include "tmag5273_mag.h"
#include "weight_model.h"
#include "weight_pipeline.h"

using namespace Adafruit_LittleFS_Namespace;

#define LOG(...)                                \
  do {                                          \
    if (Serial) Serial.printf(__VA_ARGS__);     \
  } while (0)

static_assert(BEACON_TMAG_OVERSAMPLE >= 1 && BEACON_TMAG_OVERSAMPLE <= 5,
              "BEACON_TMAG_OVERSAMPLE: 1..5 conversions (about 3.35 ms each) fit in one 20 ms sample");
// InternalFS is 28 KB and also holds Bluefruit's bonding data; two of these files must fit easily.
static_assert(gym::WeightModel::kMaxSerializedBytes <= 4096, "taught-weights file must stay under 4 KB");

namespace {
const uint32_t kSamplePeriodMs = 20;      // 50 Hz, as in the paper
const uint32_t kNotFreshRetryMs = 4;      // accelerometer had no new sample yet: look again soon
const uint8_t kMaxMagFails = 5;           // consecutive failed reads before a sensor counts as gone
const uint32_t kProbeIntervalMs = 30000;
const float kSegmentLowPassHz = 3.0f;
const int16_t kHeightFloorMm = 15;

const bool kWantTmag = BEACON_MAG_SENSOR == MagSensor::Auto || BEACON_MAG_SENSOR == MagSensor::Tmag5273 ||
                       BEACON_MAG_SENSOR == MagSensor::Both;
const bool kWantLis2mdl = BEACON_MAG_SENSOR == MagSensor::Auto || BEACON_MAG_SENSOR == MagSensor::Lis2mdl ||
                          BEACON_MAG_SENSOR == MagSensor::Both;

// One magnetometer: its driver, its taught weights, and its share of the last set.
struct MagSlot {
  Magnetometer* mag;
  const char* modelPath;
  const char* streamTag;  // Teleplot name prefix
  bool wanted;            // BEACON_MAG_SENSOR allows it
  bool present;
  bool awake;
  bool modelLoaded;
  bool inSet;             // was there when the set started
  bool lost;              // dropped out during the set
  uint8_t fails;
  int16_t tempC10;
  bool haveRest;          // last processed set
  int16_t rest[3];
  size_t featN;
  const char* error;
  // The same, frozen when a set is recorded: the CSV dump describes exactly the samples it prints,
  // even after a later set that was not recorded has changed the fields above.
  bool dumpInSet;
  bool dumpLost;
  bool dumpHaveRest;
  int16_t dumpRest[3];
  int16_t dumpTempC10;
  size_t dumpFeatN;
  const char* dumpError;
};

Tmag5273Mag s_tmag(BEACON_TMAG5273_ADDR, BEACON_TMAG_OVERSAMPLE);
Lis2mdlMag s_lis2mdl(BEACON_LIS2MDL_ADDR);
MagSlot s_slot[kMaxMagnetometers] = {
    {&s_tmag, "/w8_tmag.bin", "t", kWantTmag},
    {&s_lis2mdl, "/w8_lis2.bin", "l", kWantLis2mdl},
};
SetRecorder s_rec;
float s_aUp[kRecorderSamples];
float s_x[kRecorderSamples];
gym::SetData s_set = {};
gym::MotionAnalysis s_motion = {};
gym::RepFeatures s_feat[kMaxMagnetometers][gym::kMaxRepSegments];
gym::WeightModel s_model[kMaxMagnetometers];
uint8_t s_io[gym::WeightModel::kMaxSerializedBytes];
int16_t s_latestB[kMaxMagnetometers][3];
uint8_t s_latestValid = 0;

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

size_t labelCount(uint8_t k) {
  uint16_t ls[gym::WeightModel::kMaxLabels];
  uint16_t cs[gym::WeightModel::kMaxLabels];
  return s_model[k].labels(ls, cs, gym::WeightModel::kMaxLabels);
}

void loadModel(uint8_t k, bool flashOk) {
  MagSlot& s = s_slot[k];
  s.modelLoaded = true;
  if (!flashOk) return;
  File f = InternalFS.open(s.modelPath, FILE_O_READ);
  if (!f) return;
  const int n = f.read(s_io, sizeof(s_io));
  f.close();
  if (n <= 0 || !s_model[k].deserialize(s.mag->sensorId(), s_io, static_cast<size_t>(n))) {
    LOG("[weight] %s is from another firmware version - ignored (teach again)\n", s.modelPath);
  }
}

bool saveModel(uint8_t k, bool flashOk) {
  if (!flashOk) return false;
  const MagSlot& s = s_slot[k];
  InternalFS.remove(s.modelPath);  // FILE_O_WRITE appends, so start from an empty file
  if (s_model[k].size() == 0) return true;
  const size_t n = s_model[k].serialize(s.mag->sensorId(), s_io, sizeof(s_io));
  File f = InternalFS.open(s.modelPath, FILE_O_WRITE);
  if (!f) return false;
  const size_t written = f.write(s_io, n);
  f.close();
  return written == n;
}

// One sensor's verdict on the last set, e.g. "TMAG5273 ~50 (7/8 reps)".
void describe(uint8_t k, char* out, size_t cap) {
  const MagSlot& s = s_slot[k];
  const char* name = s.mag->name();
  if (s.lost) {
    snprintf(out, cap, "%s dropped out", name);
    return;
  }
  if (s.featN == 0) {
    snprintf(out, cap, "%s not weighed: %s", name, s.error != nullptr ? s.error : "no reps");
    return;
  }
  if (s_model[k].size() == 0) {
    snprintf(out, cap, "%s: %u reps measured, no weights taught yet", name, static_cast<unsigned>(s.featN));
    return;
  }
  const gym::WeightEstimate e = s_model[k].classify(s_feat[k], s.featN);
  char label[12];
  formatLabel(e.label, label, sizeof(label));
  if (e.decided) {
    snprintf(out, cap, "%s ~%s (%u/%u reps)", name, label, e.agree, e.votes);
  } else if (e.agree > 0) {
    snprintf(out, cap, "%s uncertain (%u/%u say %s, %u unknown)", name, e.agree, e.votes, label, e.unknown);
  } else {
    snprintf(out, cap, "%s unknown (no rep like a taught weight)", name);
  }
}
}  // namespace

// ---------------------------------------------------------------- sensors

void WeightSensing::begin(TwoWire& wire, Lis3dhMotion& imu, bool flashOk, uint32_t now) {
  wire_ = &wire;
  imu_ = &imu;
  flashOk_ = flashOk;
  if (flashOk_) InternalFS.begin();  // already mounted by Bluefruit for bonding; harmless twice
  probe(now);
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    const MagSlot& s = s_slot[k];
    if (s.wanted && !s.present) LOG("[weight] no %s at 0x%02X\n", s.mag->name(), s.mag->address());
  }
  if (!anyPresent()) LOG("[weight] no magnetometer - weight sensing off\n");
}

void WeightSensing::housekeeping(uint32_t now) {
  if (inSet_ || !elapsed(now, lastProbeMs_, kProbeIntervalMs)) return;
  probe(now);
}

void WeightSensing::probe(uint32_t now) {
  lastProbeMs_ = now;
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    MagSlot& s = s_slot[k];
    if (!s.wanted || s.present || !s.mag->begin(*wire_)) continue;  // begin() leaves it asleep
    s.present = true;
    s.awake = false;
    int16_t floors[gym::kWeightFeatureCount];
    modelFloors(*s.mag, floors);
    s_model[k].setFloors(floors);
    if (!s.modelLoaded) loadModel(k, flashOk_);
    LOG("[weight] %s at 0x%02X, %u weights taught\n", s.mag->name(), s.mag->address(),
        static_cast<unsigned>(labelCount(k)));
  }
  updatePower(now);  // the stream may already be on
}

bool WeightSensing::anyPresent() const {
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    if (s_slot[k].present) return true;
  }
  return false;
}

size_t WeightSensing::featTotal() const {
  size_t n = 0;
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) n += s_slot[k].featN;
  return n;
}

// Each magnetometer is awake exactly while the sampler needs it; the sampler runs while a set is
// recorded or the stream is on.
void WeightSensing::updatePower(uint32_t now) {
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    MagSlot& s = s_slot[k];
    const bool want = s.present && (recording_ || streaming_);
    if (want && !s.awake) {
      if (s.mag->wake()) {
        s.awake = true;
        s.fails = 0;
      } else {
        LOG("[weight] %s does not answer - off until it is back\n", s.mag->name());
        s.present = false;
        s.lost = inSet_ && s.inSet;
      }
    } else if (!want && s.awake) {
      s.mag->sleep();
      s.awake = false;
    }
  }
  if (recording_ && !anyPresent()) recording_ = false;  // nothing left to weigh this set with
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
  bool dropped = false;
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    MagSlot& slot = s_slot[k];
    if (!slot.awake) continue;
    int16_t temp = 0;
    if (slot.mag->read(b[k], &temp)) {
      valid |= static_cast<uint8_t>(1u << k);
      slot.fails = 0;
      slot.tempC10 = temp;
    } else if (++slot.fails >= kMaxMagFails) {
      LOG("[weight] %s stopped answering%s\n", slot.mag->name(), slot.inSet ? " - it does not weigh this set" : "");
      slot.present = false;
      slot.awake = false;
      slot.lost = inSet_ && slot.inSet;
      dropped = true;
    }
  }
  if (dropped) updatePower(now);
  if (recording_) s_rec.add(now, s.mg, b, valid);
  memcpy(latestAcc_, s.mg, sizeof(latestAcc_));
  memcpy(s_latestB, b, sizeof(s_latestB));
  s_latestValid = valid;
  return true;
}

void WeightSensing::printStreamFields() const {
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    if ((s_latestValid >> k) & 1u) {
      const char* t = s_slot[k].streamTag;
      Serial.printf(">%sx:%.1f\n>%sy:%.1f\n>%sz:%.1f\n", t, s_latestB[k][0] / 10.0f, t, s_latestB[k][1] / 10.0f, t,
                    s_latestB[k][2] / 10.0f);
    }
  }
}

// ---------------------------------------------------------------- set lifecycle

void WeightSensing::startSet(uint32_t now, const char* blocked) {
  if (inSet_) return;
  inSet_ = true;
  blocked_ = blocked;
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    s_slot[k].inSet = s_slot[k].present;
    s_slot[k].lost = false;
  }
  magInSet_ = anyPresent();
  if (dumping()) {
    dumpSection_ = kDumpIdle;
    LOG("# dump aborted: a new set started\n");
  }
  recording_ = magInSet_ && blocked == nullptr;
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
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) s_slot[k].featN = 0;  // `teach` must not label an earlier set
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
  lastTaught_ = false;
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    s_slot[k].featN = 0;
    s_slot[k].haveRest = false;
    s_slot[k].error = nullptr;
  }
  if (!magInSet_) return;  // no magnetometer: nothing to say
  if (blocked_ != nullptr) {
    LOG("[weight] set %u not weighed: %s\n", setSeq, blocked_);
    return;
  }
  if (!recorded) {
    LOG("[weight] set %u not weighed: the magnetometer dropped out\n", setSeq);
    return;
  }

  s_rec.finish(lastEventMs, &s_set);
  haveSet_ = true;
  rawValid_ = true;
  lastSetSeq_ = setSeq;
  lastCounted_ = countedReps;
  if (s_rec.wrapped()) LOG("[weight] set %u is longer than %u s: using its last %u s\n", setSeq,
                           BEACON_W8_MAX_SET_S, BEACON_W8_MAX_SET_S);

  gym::WeightPipelineConfig cfg;
  cfg.minRepMs = BEACON_W8_MIN_REP_MS;
  cfg.minRepIntervalMs = minRepIntervalMs;
  cfg.stillP2pMg = BEACON_STILL_P2P_MG;
  cfg.lowPassHz = kSegmentLowPassHz;
  motionError_ = nullptr;
  const bool moved = gym::analyzeMotion(s_set, cfg, s_aUp, s_x, &s_motion);
  if (!moved) motionError_ = s_motion.error;
  for (uint8_t k = 0; k < kMaxMagnetometers && moved; k++) {
    MagSlot& s = s_slot[k];
    if (!s.inSet || s.lost) continue;
    int16_t noise[3];
    s.mag->sampleNoise(noise);
    const gym::MagData md = s_rec.magData(k, noise);
    s.featN = gym::magFeatures(s_set, md, s_motion, s_feat[k], gym::kMaxRepSegments, s.rest, &s.error);
    s.haveRest = s.featN > 0;
  }
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    MagSlot& s = s_slot[k];
    s.dumpInSet = s.inSet;
    s.dumpLost = s.lost;
    s.dumpHaveRest = s.haveRest;
    memcpy(s.dumpRest, s.rest, sizeof(s.dumpRest));
    s.dumpTempC10 = s.tempC10;
    s.dumpFeatN = s.featN;
    s.dumpError = s.error;
  }
  if (logAll_ && Serial) startDump();
  report(setSeq);
}

// "[weight] set 143: TMAG5273 ~50 (7/8 reps) | LIS2MDL ~50 (8/8 reps) | lift 27 cm"
void WeightSensing::report(uint8_t setSeq) const {
  if (!Serial) return;
  if (motionError_ != nullptr) {
    Serial.printf("[weight] set %u not weighed: %s\n", setSeq, motionError_);
    return;
  }
  Serial.printf("[weight] set %u:", setSeq);
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    if (!s_slot[k].inSet) continue;
    char text[96];
    describe(k, text, sizeof(text));
    Serial.printf(" %s |", text);
  }
  Serial.printf(" lift %u cm", (medianLiftMm(s_motion) + 5) / 10);
  if (s_motion.adapted) Serial.printf(", dT %u ms", s_motion.minRepMsUsed);
  bool untaught = featTotal() > 0;
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) untaught = untaught && (s_slot[k].featN == 0 || s_model[k].size() == 0);
  Serial.println(untaught ? " - type \"teach <weight>\"" : "");
}

// ---------------------------------------------------------------- console

void WeightSensing::printHelp() const {
  Serial.println(F("  mag                  magnetometer(s), field at rest, taught weights"));
  Serial.println(F("  teach <w>            label the last set with weight w (teach 50, teach 12.5)"));
  Serial.println(F("  weights              list the taught weights"));
  Serial.println(F("  forget <w>|all       delete a taught weight"));
  Serial.println(F("  dump                 print the last set as CSV (offline training)"));
  Serial.println(F("  log                  toggle: dump every set automatically"));
}

void WeightSensing::printSummary() const {
  if (!anyPresent()) {
    Serial.println(F("weight: no magnetometer (looking again every 30 s)"));
    return;
  }
  Serial.print(F("weight:"));
  bool first = true;
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    const MagSlot& s = s_slot[k];
    if (!s.present) continue;
    Serial.printf("%s %s at 0x%02X, %u weights taught (%u reps)", first ? "" : ";", s.mag->name(), s.mag->address(),
                  static_cast<unsigned>(labelCount(k)), static_cast<unsigned>(s_model[k].size()));
    first = false;
  }
  Serial.println(recording_ ? ", recording" : "");
}

void WeightSensing::printWeights() const {
  bool any = false;
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    const MagSlot& s = s_slot[k];
    if (!s.wanted || (!s.present && s_model[k].size() == 0)) continue;
    any = true;
    uint16_t ls[gym::WeightModel::kMaxLabels];
    uint16_t cs[gym::WeightModel::kMaxLabels];
    const size_t n = s_model[k].labels(ls, cs, gym::WeightModel::kMaxLabels);
    Serial.printf("weights %s:", s.mag->name());
    if (n == 0) Serial.print(F(" none taught (do a set, then: teach <weight>)"));
    for (size_t i = 0; i < n; i++) {
      char label[12];
      formatLabel(ls[i], label, sizeof(label));
      Serial.printf(" %s (%u reps)%s", label, cs[i], i + 1 < n ? "," : "");
    }
    Serial.println();
  }
  if (!any) Serial.println(F("weights: no magnetometer"));
}

void WeightSensing::printStatus() const {
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    const MagSlot& s = s_slot[k];
    if (!s.wanted) continue;
    if (!s.present) {
      Serial.printf("%s: none at 0x%02X (looking again every 30 s)\n", s.mag->name(), s.mag->address());
      continue;
    }
    Serial.printf("%s at 0x%02X: %s, %.1f C\n", s.mag->name(), s.mag->address(), s.awake ? "sampling" : "asleep",
                  s.tempC10 / 10.0f);
    if (s.haveRest) {
      Serial.printf("  field at rest after set %u: x %.1f, y %.1f, z %.1f uT\n", lastSetSeq_, s.rest[0] / 10.0f,
                    s.rest[1] / 10.0f, s.rest[2] / 10.0f);
    }
  }
  if (recording_) {
    Serial.printf("recording: %.1f s, %u samples\n", (millis() - recordStartMs_) / 1000.0f,
                  static_cast<unsigned>(s_rec.size()));
  } else if (inSet_ && blocked_ != nullptr) {
    Serial.printf("this set is not weighed: %s\n", blocked_);
  }
  if (!lastTaught_ && featTotal() > 0) Serial.printf("last set %u is ready to teach\n", lastSetSeq_);
  Serial.printf("auto-dump %s, taught weights %s\n", logAll_ ? "on" : "off",
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
    char text[12];
    if (!parseLabel(arg, &label)) {
      Serial.println(F("usage: teach <weight>   e.g. teach 50 or teach 12.5"));
    } else if (featTotal() == 0) {
      if (lastEndedSeq_ != 0) {
        Serial.printf("set %u was not weighed - nothing to teach (do another set)\n", lastEndedSeq_);
      } else {
        Serial.println(F("no weighed set to teach from - do a set first"));
      }
    } else if (lastTaught_) {
      Serial.println(F("that set is already taught - do another set"));
    } else {
      formatLabel(label, text, sizeof(text));
      lastTaught_ = true;
      for (uint8_t k = 0; k < kMaxMagnetometers; k++) {  // every sensor that measured the set learns it
        const MagSlot& s = s_slot[k];
        if (s.featN == 0) continue;
        const size_t added = s_model[k].add(label, s_feat[k], s.featN);
        if (added == 0) {
          Serial.printf("%s: too many different weights (%u) - forget one first\n", s.mag->name(),
                        static_cast<unsigned>(gym::WeightModel::kMaxLabels));
          continue;
        }
        const bool saved = saveModel(k, flashOk_);
        Serial.printf("%s taught %s: %u reps of set %u (%u reps in total)%s\n", s.mag->name(), text,
                      static_cast<unsigned>(added), lastSetSeq_, static_cast<unsigned>(s_model[k].size()),
                      saved ? "" : " - NOT saved to flash");
      }
    }
  } else if (strcmp(cmd, "forget") == 0) {
    uint16_t label = 0;
    const bool all = arg != nullptr && strcmp(arg, "all") == 0;
    if (!all && !parseLabel(arg, &label)) {
      Serial.println(F("usage: forget <weight>|all"));
      return true;
    }
    size_t removed = 0;
    for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
      // A sensor that was never found has not loaded its file: leave that file alone.
      if (!s_slot[k].modelLoaded) continue;
      const size_t r = all ? s_model[k].size() : s_model[k].forget(label);
      if (all) s_model[k].clear();
      if (r > 0) saveModel(k, flashOk_);
      removed += r;
    }
    Serial.printf("deleted %u reps\n", static_cast<unsigned>(removed));
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
      snprintf(line, cap, "# w8 set=%u reps=%u samples=%u sensors=%s%s%s dT_ms=%u adapted=%u\n", lastSetSeq_,
               lastCounted_, static_cast<unsigned>(s_set.n), s_slot[0].dumpInSet ? "tmag" : "",
               s_slot[0].dumpInSet && s_slot[1].dumpInSet ? "+" : "", s_slot[1].dumpInSet ? "lis2mdl" : "",
               s_motion.minRepMsUsed, s_motion.adapted ? 1 : 0);
      dumpSection_ = kDumpError;
      return true;
    case kDumpError:  // own line: the longest message would not fit after the summary
      snprintf(line, cap, "# error=%s\n", motionError_ != nullptr ? motionError_ : "none");
      dumpSection_ = kDumpRest;
      return true;
    case kDumpRest:
      dumpSection_ = kDumpMagRest;
      dumpIndex_ = 0;
      if (s_motion.restEnd == 0) {  // the set was rejected before a rest window was found
        snprintf(line, cap, "# rest=none\n");
        return true;
      }
      snprintf(line, cap, "# rest=%u..%u gravity_mg=%.1f,%.1f,%.1f\n", s_motion.restStart, s_motion.restEnd,
               s_motion.gravityMg[0], s_motion.gravityMg[1], s_motion.gravityMg[2]);
      return true;
    case kDumpMagRest: {  // two lines per sensor that measured the set: its rest field, its error
      if (dumpIndex_ >= 2u * kMaxMagnetometers) {
        dumpSection_ = kDumpCounts;
        dumpIndex_ = 0;
        return false;
      }
      const size_t part = dumpIndex_ % 2;
      const MagSlot& s = s_slot[dumpIndex_ / 2];
      dumpIndex_++;
      if (!s.dumpInSet) return false;
      if (part == 1) {
        snprintf(line, cap, "# %s error=%s\n", s.mag->name(),
                 s.dumpLost ? "dropped out" : (s.dumpError != nullptr ? s.dumpError : "none"));
      } else if (s.dumpHaveRest) {
        snprintf(line, cap, "# %s b_rest_0.1uT=%d,%d,%d temp_c=%.1f reps=%u\n", s.mag->name(), s.dumpRest[0],
                 s.dumpRest[1], s.dumpRest[2], s.dumpTempC10 / 10.0f, static_cast<unsigned>(s.dumpFeatN));
      } else {
        snprintf(line, cap, "# %s b_rest_0.1uT=none temp_c=%.1f reps=0\n", s.mag->name(), s.dumpTempC10 / 10.0f);
      }
      return true;
    }
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
      // tx..tz: TMAG5273, lx..lz: LIS2MDL (0.1 uT); valid: bit 0 TMAG5273, bit 1 LIS2MDL
      snprintf(line, cap, "t_ms,ax_mg,ay_mg,az_mg,tx,ty,tz,lx,ly,lz,valid\n");
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
      const int16_t* t = s_rec.mag(0, i);
      const int16_t* l = s_rec.mag(1, i);
      snprintf(line, cap, "%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u\n", s_rec.timeMs(i), a[0], a[1], a[2], t[0], t[1], t[2],
               l[0], l[1], l[2], s_rec.flags(i));
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
