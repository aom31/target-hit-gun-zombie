#include "crosstalk_test.h"

#include <algorithm>

namespace tr::hub {

namespace {
constexpr uint8_t kNoiseFactor = 4;
constexpr uint8_t kHitShareX10 = 4;  // threshold at 40% of the weakest real strike
constexpr uint16_t kMinThreshold = 30;
constexpr uint16_t kMaxThreshold = 4000;
}  // namespace

void CrosstalkTest::start(uint8_t nodeId, uint8_t shotsPerZone) {
  active_ = true;
  done_ = false;
  nodeId_ = nodeId;
  zone_ = 0;
  shot_ = 0;
  shotsPerZone_ = shotsPerZone == 0 ? 1 : shotsPerZone;
  for (auto& row : sum_) row.fill(0);
  count_.fill(0);
  minHit_.fill(0);
}

bool CrosstalkTest::record(const std::array<uint16_t, kZoneCount>& peaks) {
  if (!active_) return false;
  for (uint8_t ch = 0; ch < kZoneCount; ++ch) sum_[zone_][ch] += peaks[ch];
  ++count_[zone_];
  const uint16_t struck = peaks[zone_];
  if (minHit_[zone_] == 0 || struck < minHit_[zone_]) minHit_[zone_] = struck;

  if (++shot_ < shotsPerZone_) return false;
  shot_ = 0;
  if (++zone_ >= kZoneCount) {
    zone_ = kZoneCount - 1;
    active_ = false;
    done_ = true;
  }
  return true;
}

CrosstalkReport CrosstalkTest::report(const std::array<uint16_t, kZoneCount>& noise) const {
  CrosstalkReport r;
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    for (uint8_t ch = 0; ch < kZoneCount; ++ch) {
      r.avg[z][ch] = count_[z] == 0 ? 0 : static_cast<uint16_t>(sum_[z][ch] / count_[z]);
    }
  }
  r.minHit = minHit_;
  for (uint8_t ch = 0; ch < kZoneCount; ++ch) {
    const uint32_t fromNoise = static_cast<uint32_t>(noise[ch]) * kNoiseFactor;
    const uint32_t fromHits = static_cast<uint32_t>(minHit_[ch]) * kHitShareX10 / 10;
    uint32_t t = std::max<uint32_t>(fromNoise, fromHits);
    t = std::max<uint32_t>(t, kMinThreshold);
    r.suggested[ch] = static_cast<uint16_t>(std::min<uint32_t>(t, kMaxThreshold));

    uint16_t strongestNeighbour = 1;
    for (uint8_t other = 0; other < kZoneCount; ++other) {
      if (other != ch) strongestNeighbour = std::max(strongestNeighbour, r.avg[ch][other]);
    }
    const uint32_t ratio = static_cast<uint32_t>(r.avg[ch][ch]) * 10 / strongestNeighbour;
    r.ratioX10[ch] = static_cast<uint16_t>(std::min<uint32_t>(ratio, 999));
  }
  return r;
}

}  // namespace tr::hub
