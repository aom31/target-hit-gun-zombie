#pragma once

// Guided sensor calibration: light one zone, the operator shoots it N times,
// record every channel's peak, move on. The 5x5 matrix shows how strongly a
// strike on one zone leaks into the others and yields suggested thresholds.

#include <array>
#include <cstdint>

#include "system_config.h"

namespace tr::hub {

struct CrosstalkReport {
  std::array<std::array<uint16_t, kZoneCount>, kZoneCount> avg{};  // [struck zone][channel]
  std::array<uint16_t, kZoneCount> minHit{};                       // weakest strike seen per zone
  std::array<uint16_t, kZoneCount> suggested{};                    // threshold per channel
  std::array<uint16_t, kZoneCount> ratioX10{};  // struck / strongest neighbour, x10 (30 = 3.0)
};

class CrosstalkTest {
 public:
  static constexpr uint8_t kMinRatioX10 = 30;

  void start(uint8_t nodeId, uint8_t shotsPerZone);
  void cancel() { active_ = false; }
  // Records one strike; returns true if the zone under test advanced.
  bool record(const std::array<uint16_t, kZoneCount>& peaks);

  bool active() const { return active_; }
  bool done() const { return done_; }
  uint8_t nodeId() const { return nodeId_; }
  uint8_t zone() const { return zone_; }
  uint8_t shot() const { return shot_; }
  uint8_t shotsPerZone() const { return shotsPerZone_; }
  CrosstalkReport report(const std::array<uint16_t, kZoneCount>& noise) const;

 private:
  bool active_ = false;
  bool done_ = false;
  uint8_t nodeId_ = 1;
  uint8_t zone_ = 0;
  uint8_t shot_ = 0;
  uint8_t shotsPerZone_ = 5;
  std::array<std::array<uint32_t, kZoneCount>, kZoneCount> sum_{};
  std::array<uint8_t, kZoneCount> count_{};
  std::array<uint16_t, kZoneCount> minHit_{};
};

}  // namespace tr::hub
