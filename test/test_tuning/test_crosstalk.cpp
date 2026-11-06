#include <unity.h>

#include "crosstalk_test.h"

using namespace tr;
using namespace tr::hub;

namespace {
std::array<uint16_t, kZoneCount> strike(uint8_t zone, uint16_t hit, uint16_t leak) {
  std::array<uint16_t, kZoneCount> p{};
  for (uint8_t ch = 0; ch < kZoneCount; ++ch) p[ch] = ch == zone ? hit : leak;
  return p;
}
}  // namespace

void setUp() {}
void tearDown() {}

void test_walks_all_zones_then_finishes() {
  CrosstalkTest t;
  t.start(2, 3);
  int advances = 0;
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    TEST_ASSERT_EQUAL_UINT8(z, t.zone());
    for (uint8_t s = 0; s < 3; ++s) advances += t.record(strike(z, 1000, 100)) ? 1 : 0;
  }
  TEST_ASSERT_EQUAL(5, advances);
  TEST_ASSERT_FALSE(t.active());
  TEST_ASSERT_TRUE(t.done());
  TEST_ASSERT_FALSE(t.record(strike(0, 1000, 100)));  // ignored once finished
}

void test_report_averages_ratios_and_suggestions() {
  CrosstalkTest t;
  t.start(1, 2);
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    t.record(strike(z, 1000, 100));
    t.record(strike(z, 800, 300));
  }
  const CrosstalkReport r = t.report({{10, 10, 10, 10, 10}});
  TEST_ASSERT_EQUAL_UINT16(900, r.avg[2][2]);
  TEST_ASSERT_EQUAL_UINT16(200, r.avg[2][0]);
  TEST_ASSERT_EQUAL_UINT16(800, r.minHit[3]);
  TEST_ASSERT_EQUAL_UINT16(45, r.ratioX10[1]);       // 900 / 200 = 4.5x
  TEST_ASSERT_EQUAL_UINT16(320, r.suggested[4]);     // 40% of the weakest strike (800)
}

void test_noise_floor_raises_suggestion() {
  CrosstalkTest t;
  t.start(1, 1);
  for (uint8_t z = 0; z < kZoneCount; ++z) t.record(strike(z, 200, 20));
  const CrosstalkReport r = t.report({{50, 5, 5, 5, 5}});
  TEST_ASSERT_EQUAL_UINT16(200, r.suggested[0]);  // 4 x noise 50 beats 40% of 200
  TEST_ASSERT_EQUAL_UINT16(80, r.suggested[1]);
}

void test_poor_isolation_shows_low_ratio() {
  CrosstalkTest t;
  t.start(1, 1);
  for (uint8_t z = 0; z < kZoneCount; ++z) t.record(strike(z, 600, 400));
  const CrosstalkReport r = t.report({{5, 5, 5, 5, 5}});
  TEST_ASSERT_TRUE(r.ratioX10[0] < CrosstalkTest::kMinRatioX10);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_walks_all_zones_then_finishes);
  RUN_TEST(test_report_averages_ratios_and_suggestions);
  RUN_TEST(test_noise_floor_raises_suggestion);
  RUN_TEST(test_poor_isolation_shows_low_ratio);
  return UNITY_END();
}
