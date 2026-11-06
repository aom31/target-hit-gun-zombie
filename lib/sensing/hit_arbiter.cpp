#include "hit_arbiter.h"

#include <algorithm>

namespace tr::sensing {

namespace {
constexpr float kBaselineAlpha = 0.001f;  // ~0.5 s time constant at 2 kHz
constexpr float kNoiseAlpha = 0.01f;
constexpr float kReleaseRatio = 0.5f;
constexpr uint8_t kQuietFramesToRelease = 3;
}  // namespace

void HitArbiter::startCalibration(uint16_t samples) {
  calRemaining_ = samples == 0 ? 1 : samples;
  calTotal_ = calRemaining_;
  calSum_.fill(0);
  phase_ = Phase::Idle;
}

uint16_t HitArbiter::trigger(uint8_t ch) const {
  if (!tuning_) return cfg_.threshold[ch];
  const uint16_t fromNoise = static_cast<uint16_t>(noise_[ch] * kTuneNoiseFactor);
  return std::max<uint16_t>(fromNoise, kTuneFloor);
}

bool HitArbiter::process(const std::array<uint16_t, kZoneCount>& raw, uint32_t nowUs, Detection& out) {
  if (calRemaining_ > 0) {
    for (uint8_t ch = 0; ch < kZoneCount; ++ch) calSum_[ch] += raw[ch];
    if (--calRemaining_ == 0) {
      for (uint8_t ch = 0; ch < kZoneCount; ++ch) {
        baseline_[ch] = static_cast<float>(calSum_[ch]) / static_cast<float>(calTotal_);
        noise_[ch] = 2.0f;
      }
      calibrated_ = true;
    }
    return false;
  }
  if (!calibrated_) return false;

  std::array<uint16_t, kZoneCount> delta{};
  for (uint8_t ch = 0; ch < kZoneCount; ++ch) {
    const float d = static_cast<float>(raw[ch]) - baseline_[ch];
    delta[ch] = static_cast<uint16_t>(d < 0 ? -d : d);
  }

  switch (phase_) {
    case Phase::Idle: {
      if (!reached(nowUs, guardUntilUs_)) return false;
      bool triggered = false;
      for (uint8_t ch = 0; ch < kZoneCount; ++ch) {
        if (delta[ch] >= trigger(ch)) triggered = true;
      }
      if (!triggered) {
        adapt(raw, delta);
        return false;
      }
      peaks_ = delta;
      detectedAtUs_ = nowUs;
      captureEndsUs_ = nowUs + static_cast<uint32_t>(cfg_.windowMs) * 1000u;
      phase_ = Phase::Capture;
      return false;
    }

    case Phase::Capture: {
      for (uint8_t ch = 0; ch < kZoneCount; ++ch) peaks_[ch] = std::max(peaks_[ch], delta[ch]);
      if (!reached(nowUs, captureEndsUs_)) return false;

      // Compare channels on their own scale: piezo discs differ in sensitivity.
      int8_t winner = -1;
      uint32_t bestScore = 0;
      for (uint8_t ch = 0; ch < kZoneCount; ++ch) {
        const uint16_t trig = std::max<uint16_t>(trigger(ch), 1);
        if (peaks_[ch] < trig) continue;
        const uint32_t score = static_cast<uint32_t>(peaks_[ch]) * 1024u / trig;
        if (winner < 0 || score > bestScore) {
          winner = static_cast<int8_t>(ch);
          bestScore = score;
        }
      }

      phase_ = Phase::Settle;
      lockoutEndsUs_ = detectedAtUs_ + static_cast<uint32_t>(cfg_.lockoutMs) * 1000u;
      quietFrames_ = 0;
      if (winner < 0) return false;  // trigger was a spike that vanished before the window closed

      out.zone = static_cast<uint8_t>(winner);
      out.peaks = peaks_;
      out.atUs = detectedAtUs_;
      out.suppressedMask = 0;
      for (uint8_t ch = 0; ch < kZoneCount; ++ch) {
        if (ch != winner && peaks_[ch] >= trigger(ch)) out.suppressedMask |= static_cast<uint8_t>(1u << ch);
      }
      return true;
    }

    case Phase::Settle: {
      if (!reached(nowUs, lockoutEndsUs_)) return false;
      bool quiet = true;
      for (uint8_t ch = 0; ch < kZoneCount; ++ch) {
        if (delta[ch] >= static_cast<uint16_t>(trigger(ch) * kReleaseRatio)) quiet = false;
      }
      quietFrames_ = quiet ? static_cast<uint8_t>(quietFrames_ + 1) : 0;
      if (quietFrames_ >= kQuietFramesToRelease || reached(nowUs, lockoutEndsUs_ + kMaxSettleUs)) {
        phase_ = Phase::Idle;
      }
      return false;
    }
  }
  return false;
}

void HitArbiter::adapt(const std::array<uint16_t, kZoneCount>& raw, const std::array<uint16_t, kZoneCount>& delta) {
  for (uint8_t ch = 0; ch < kZoneCount; ++ch) {
    baseline_[ch] += kBaselineAlpha * (static_cast<float>(raw[ch]) - baseline_[ch]);
    noise_[ch] += kNoiseAlpha * (static_cast<float>(delta[ch]) - noise_[ch]);
  }
}

}  // namespace tr::sensing
