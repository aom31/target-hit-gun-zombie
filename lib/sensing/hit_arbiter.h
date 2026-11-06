#pragma once

// Turns raw ADC frames from the five piezo channels into zone hits.
//
// One BB strikes one zone, but the impact rings through the whole mannequin.
// When any channel crosses its threshold the arbiter opens a short window,
// collects the peak of every channel, and awards the hit to the channel with
// the largest peak relative to its own threshold. The whole mannequin is then
// locked out until the signal settles. No Arduino dependencies.

#include <array>
#include <cstdint>

#include "system_config.h"

namespace tr::sensing {

struct ArbiterConfig {
  std::array<uint16_t, kZoneCount> threshold{{kDefaultThreshold, kDefaultThreshold, kDefaultThreshold,
                                              kDefaultThreshold, kDefaultThreshold}};
  uint16_t lockoutMs = kDefaultLockoutMs;
  uint8_t windowMs = kDefaultArbiterWindowMs;
};

struct Detection {
  uint8_t zone = 0;
  uint8_t suppressedMask = 0;  // other channels that also crossed their threshold
  std::array<uint16_t, kZoneCount> peaks{};
  uint32_t atUs = 0;
};

class HitArbiter {
 public:
  static constexpr uint16_t kTuneFloor = 25;   // minimum trigger level in tuning mode
  static constexpr uint8_t kTuneNoiseFactor = 4;
  static constexpr uint32_t kMaxSettleUs = 250000;

  void configure(const ArbiterConfig& cfg) { cfg_ = cfg; }
  const ArbiterConfig& config() const { return cfg_; }

  // Tuning mode triggers just above the noise floor so weak hits are visible.
  void setTuning(bool on) { tuning_ = on; }
  bool tuning() const { return tuning_; }

  // Averages the next `samples` frames into a fresh baseline. Don't touch the target meanwhile.
  void startCalibration(uint16_t samples);
  bool calibrated() const { return calibrated_; }
  bool calibrating() const { return calRemaining_ > 0; }

  // Ignore input until this time (used right after the zone lights change).
  void guardUntil(uint32_t us) { guardUntilUs_ = us; }

  // Feed one frame (one sample per channel). Returns true when a hit is decided.
  bool process(const std::array<uint16_t, kZoneCount>& raw, uint32_t nowUs, Detection& out);

  uint16_t baseline(uint8_t ch) const { return static_cast<uint16_t>(baseline_[ch] + 0.5f); }
  uint16_t noise(uint8_t ch) const { return static_cast<uint16_t>(noise_[ch] + 0.5f); }
  uint16_t trigger(uint8_t ch) const;

 private:
  enum class Phase : uint8_t { Idle, Capture, Settle };

  static bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }
  void adapt(const std::array<uint16_t, kZoneCount>& raw, const std::array<uint16_t, kZoneCount>& delta);

  ArbiterConfig cfg_;
  Phase phase_ = Phase::Idle;
  bool tuning_ = false;
  bool calibrated_ = false;
  uint16_t calRemaining_ = 0;
  uint16_t calTotal_ = 0;
  std::array<uint32_t, kZoneCount> calSum_{};
  std::array<float, kZoneCount> baseline_{};
  std::array<float, kZoneCount> noise_{};
  std::array<uint16_t, kZoneCount> peaks_{};
  uint32_t captureEndsUs_ = 0;
  uint32_t lockoutEndsUs_ = 0;
  uint32_t detectedAtUs_ = 0;
  uint32_t guardUntilUs_ = 0;
  uint8_t quietFrames_ = 0;
};

}  // namespace tr::sensing
