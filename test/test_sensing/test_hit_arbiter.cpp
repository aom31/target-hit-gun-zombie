#include <unity.h>

#include "hit_arbiter.h"

using namespace tr;
using namespace tr::sensing;

namespace {

constexpr uint32_t kFrameUs = 500;  // 2 kHz per channel, like the node
constexpr uint16_t kRest = 40;

HitArbiter* arb;
uint32_t nowUs;

using Frame = std::array<uint16_t, kZoneCount>;

Frame rest() { return Frame{{kRest, kRest, kRest, kRest, kRest}}; }

// Feeds `frames` copies of `f`; returns the number of detections and keeps the last one.
int feed(const Frame& f, int frames, Detection* last = nullptr) {
  int hits = 0;
  for (int i = 0; i < frames; ++i) {
    Detection d;
    if (arb->process(f, nowUs, d)) {
      ++hits;
      if (last) *last = d;
    }
    nowUs += kFrameUs;
  }
  return hits;
}

// A strike: a decaying ring on the struck channel plus weaker crosstalk elsewhere.
int strike(uint8_t zone, uint16_t amplitude, uint16_t crosstalk, Detection* last = nullptr) {
  int hits = 0;
  for (int i = 0; i < 30; ++i) {
    const uint16_t decay = static_cast<uint16_t>(amplitude * (30 - i) / 30);
    const uint16_t xt = static_cast<uint16_t>(crosstalk * (30 - i) / 30);
    Frame f = rest();
    for (uint8_t ch = 0; ch < kZoneCount; ++ch) f[ch] = static_cast<uint16_t>(kRest + (ch == zone ? decay : xt));
    hits += feed(f, 1, last);
  }
  return hits;
}

void calibrate() {
  arb->startCalibration(100);
  feed(rest(), 100);
  TEST_ASSERT_TRUE(arb->calibrated());
}

}  // namespace

void setUp() {
  arb = new HitArbiter();
  nowUs = 1000000;
  ArbiterConfig cfg;
  cfg.threshold.fill(300);
  cfg.lockoutMs = 60;
  cfg.windowMs = 8;
  arb->configure(cfg);
}

void tearDown() { delete arb; }

void test_calibration_sets_baseline() {
  calibrate();
  TEST_ASSERT_EQUAL_UINT16(kRest, arb->baseline(0));
  TEST_ASSERT_EQUAL_UINT16(kRest, arb->baseline(4));
}

void test_no_detection_before_calibration() {
  Frame f = rest();
  f[2] = 3000;
  TEST_ASSERT_EQUAL(0, feed(f, 50));
}

void test_quiet_input_never_triggers() {
  calibrate();
  TEST_ASSERT_EQUAL(0, feed(rest(), 20000));
}

void test_strike_detected_on_struck_zone() {
  calibrate();
  Detection d;
  TEST_ASSERT_EQUAL(1, strike(3, 1500, 0, &d));
  TEST_ASSERT_EQUAL_UINT8(3, d.zone);
  TEST_ASSERT_TRUE(d.peaks[3] >= 1400);
}

void test_crosstalk_loses_to_struck_zone() {
  calibrate();
  Detection d;
  TEST_ASSERT_EQUAL(1, strike(1, 1500, 500, &d));
  TEST_ASSERT_EQUAL_UINT8(1, d.zone);
  // Neighbours crossed their threshold too and are reported as suppressed.
  TEST_ASSERT_EQUAL_UINT8(0b11101, d.suppressedMask);
}

void test_winner_is_relative_to_each_threshold() {
  ArbiterConfig cfg = arb->config();
  cfg.threshold = {{300, 300, 1200, 300, 300}};  // channel 2 is a much hotter piezo
  arb->configure(cfg);
  calibrate();
  Frame f = rest();
  f[0] = kRest + 700;   // 2.3x its threshold
  f[2] = kRest + 1300;  // 1.08x its threshold
  Detection d;
  TEST_ASSERT_EQUAL(1, feed(f, 30, &d));
  TEST_ASSERT_EQUAL_UINT8(0, d.zone);
}

void test_one_strike_counts_once() {
  calibrate();
  TEST_ASSERT_EQUAL(1, strike(0, 2000, 300));
  TEST_ASSERT_EQUAL(0, feed(rest(), 400));
}

void test_rapid_fire_counts_each_round_after_lockout() {
  calibrate();
  int hits = 0;
  for (int shot = 0; shot < 5; ++shot) {
    hits += strike(2, 1500, 200);
    hits += feed(rest(), 120);  // 60 ms between rounds ~ 16 rounds/s
  }
  TEST_ASSERT_EQUAL(5, hits);
}

void test_guard_window_suppresses_led_glitch() {
  calibrate();
  arb->guardUntil(nowUs + 3000);
  Frame f = rest();
  f[4] = kRest + 900;
  TEST_ASSERT_EQUAL(0, feed(f, 4));  // 2 ms inside the guard
}

void test_tuning_mode_triggers_near_noise_floor() {
  calibrate();
  Frame weak = rest();
  weak[1] = kRest + 120;  // well below the 300 threshold
  TEST_ASSERT_EQUAL(0, feed(weak, 30));
  feed(rest(), 400);
  arb->setTuning(true);
  Detection d;
  TEST_ASSERT_EQUAL(1, feed(weak, 30, &d));
  TEST_ASSERT_EQUAL_UINT8(1, d.zone);
}

void test_baseline_follows_slow_drift() {
  calibrate();
  Frame drift = rest();
  drift.fill(kRest + 100);  // below threshold, sustained
  feed(drift, 20000);
  TEST_ASSERT_UINT16_WITHIN(5, kRest + 100, arb->baseline(0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_calibration_sets_baseline);
  RUN_TEST(test_no_detection_before_calibration);
  RUN_TEST(test_quiet_input_never_triggers);
  RUN_TEST(test_strike_detected_on_struck_zone);
  RUN_TEST(test_crosstalk_loses_to_struck_zone);
  RUN_TEST(test_winner_is_relative_to_each_threshold);
  RUN_TEST(test_one_strike_counts_once);
  RUN_TEST(test_rapid_fire_counts_each_round_after_lockout);
  RUN_TEST(test_guard_window_suppresses_led_glitch);
  RUN_TEST(test_tuning_mode_triggers_near_noise_floor);
  RUN_TEST(test_baseline_follows_slow_drift);
  return UNITY_END();
}
