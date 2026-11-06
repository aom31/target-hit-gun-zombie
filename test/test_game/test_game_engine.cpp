#include <unity.h>

#include <vector>

#include "game_engine.h"

using namespace tr;
using namespace tr::game;

namespace {

struct LedCall {
  uint8_t lane;
  uint8_t zoneMask;
  LedSteady steady;
  LedColor flash;
  uint16_t flashMs;
};

class RecordingListener : public GameListener {
 public:
  void setLaneLeds(uint8_t lane, uint8_t zoneMask, const LedSteady& steady, LedColor flash,
                   uint16_t flashMs) override {
    leds.push_back({lane, zoneMask, steady, flash, flashMs});
  }
  void onGameEvent(const GameEvent& e) override { events.push_back(e); }
  void onGameFinished(const GameResult& r) override {
    results.push_back(r);
  }

  std::vector<LedCall> leds;
  std::vector<GameEvent> events;
  std::vector<GameResult> results;
};

const uint32_t kCountdown = GameSettings{}.countdownMs;

RecordingListener* listener;
GameEngine* engine;

StartRequest soloRequest(uint16_t durationS = 30) {
  StartRequest r;
  r.mode = GameMode::Solo;
  r.difficulty = Difficulty::Normal;
  r.durationS = durationS;
  r.names[0] = "Ploy";
  r.seed = 1234;
  return r;
}

StartRequest duelRequest() {
  StartRequest r = soloRequest();
  r.mode = GameMode::Duel;
  r.names[1] = "Beam";
  return r;
}

// Advances the engine in 10 ms ticks, like the hub's game task.
uint32_t runUntil(uint32_t from, uint32_t to) {
  for (uint32_t t = from; t <= to; t += 10) engine->tick(t);
  return to;
}

// Starts a game at t=0 and runs through the countdown into the first lit targets.
uint32_t startAndLightBoth(const StartRequest& req) {
  TEST_ASSERT_EQUAL(static_cast<int>(StartError::None), static_cast<int>(engine->start(req, 0)));
  uint32_t t = runUntil(0, kCountdown);
  TEST_ASSERT_EQUAL(static_cast<int>(GameState::Running), static_cast<int>(engine->state()));
  while (engine->snapshot(t).lanes[0].phase != LanePhase::Active ||
         engine->snapshot(t).lanes[1].phase != LanePhase::Active) {
    t += 10;
    engine->tick(t);
    TEST_ASSERT_TRUE_MESSAGE(t < 10000, "targets never lit");
  }
  return t;
}

}  // namespace

void setUp() {
  listener = new RecordingListener();
  engine = new GameEngine(*listener);
}

void tearDown() {
  delete engine;
  delete listener;
}

void test_rejects_invalid_requests() {
  StartRequest r = soloRequest();
  r.durationS = 5;
  TEST_ASSERT_EQUAL(static_cast<int>(StartError::BadDuration), static_cast<int>(engine->start(r, 0)));

  r = soloRequest();
  r.names[0] = "";
  TEST_ASSERT_EQUAL(static_cast<int>(StartError::BadName), static_cast<int>(engine->start(r, 0)));

  r = duelRequest();
  r.names[1] = "";
  TEST_ASSERT_EQUAL(static_cast<int>(StartError::BadName), static_cast<int>(engine->start(r, 0)));

  r = soloRequest();
  r.zoneMask[1] = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(StartError::NoZones), static_cast<int>(engine->start(r, 0)));
  TEST_ASSERT_EQUAL(static_cast<int>(GameState::Idle), static_cast<int>(engine->state()));
}

void test_rejects_start_while_active() {
  TEST_ASSERT_EQUAL(static_cast<int>(StartError::None), static_cast<int>(engine->start(soloRequest(), 0)));
  TEST_ASSERT_EQUAL(static_cast<int>(StartError::Busy), static_cast<int>(engine->start(soloRequest(), 10)));
}

void test_countdown_then_running_with_blinking_lights() {
  engine->start(soloRequest(), 0);
  TEST_ASSERT_EQUAL(static_cast<int>(GameState::Countdown), static_cast<int>(engine->state()));
  TEST_ASSERT_FALSE(listener->leds.empty());
  TEST_ASSERT_EQUAL(static_cast<int>(LedFx::Blink), static_cast<int>(listener->leds.front().steady.fx));
  runUntil(0, kCountdown - 10);
  TEST_ASSERT_EQUAL(static_cast<int>(GameState::Countdown), static_cast<int>(engine->state()));
  engine->tick(kCountdown);
  TEST_ASSERT_EQUAL(static_cast<int>(GameState::Running), static_cast<int>(engine->state()));
  TEST_ASSERT_EQUAL_UINT32(30000, engine->snapshot(kCountdown).remainingMs);
}

void test_hit_on_lit_zone_scores_zone_points() {
  engine->start(soloRequest(), 0);
  uint32_t t = runUntil(0, kCountdown);
  while (engine->snapshot(t).lanes[0].phase != LanePhase::Active) engine->tick(t += 10);
  const auto snap = engine->snapshot(t);  // t is the moment lane 0 lit
  const uint8_t zone = static_cast<uint8_t>(snap.lanes[0].target);
  const auto& points = engine->settings().zonePoints;

  engine->onHit(0, zone, t + 400, t + 410);

  const auto after = engine->snapshot(t + 410);
  TEST_ASSERT_EQUAL_INT32(points[zone], after.players[0].score);
  TEST_ASSERT_EQUAL_UINT16(1, after.players[0].hits);
  TEST_ASSERT_EQUAL_UINT32(400, after.players[0].rtBestMs);
  TEST_ASSERT_EQUAL(static_cast<int>(LanePhase::FeedbackHit), static_cast<int>(after.lanes[0].phase));
  TEST_ASSERT_EQUAL(static_cast<int>(EventKind::Hit), static_cast<int>(listener->events.back().kind));
  const LedCall& led = listener->leds.back();
  TEST_ASSERT_EQUAL(static_cast<int>(LedColor::Blue), static_cast<int>(led.flash));
  TEST_ASSERT_EQUAL(static_cast<int>(LedFx::Off), static_cast<int>(led.steady.fx));
}

void test_hit_on_unlit_zone_is_wrong_and_scores_nothing() {
  uint32_t t = startAndLightBoth(soloRequest());
  const uint8_t target = static_cast<uint8_t>(engine->snapshot(t).lanes[0].target);
  const uint8_t other = static_cast<uint8_t>((target + 1) % kZoneCount);

  engine->onHit(0, other, t + 100, t + 110);

  const auto s = engine->snapshot(t + 110);
  TEST_ASSERT_EQUAL_INT32(0, s.players[0].score);
  TEST_ASSERT_EQUAL_UINT16(1, s.players[0].wrong);
  TEST_ASSERT_EQUAL(static_cast<int>(LanePhase::Active), static_cast<int>(s.lanes[0].phase));
  TEST_ASSERT_EQUAL(static_cast<int>(EventKind::Wrong), static_cast<int>(listener->events.back().kind));
  TEST_ASSERT_EQUAL(static_cast<int>(LedColor::Red), static_cast<int>(listener->leds.back().flash));
}

void test_wrong_penalty_is_applied_when_configured() {
  GameSettings s;
  s.wrongPenalty = 2;
  TEST_ASSERT_TRUE(engine->setSettings(s));
  uint32_t t = startAndLightBoth(soloRequest());
  const uint8_t target = static_cast<uint8_t>(engine->snapshot(t).lanes[0].target);
  engine->onHit(0, static_cast<uint8_t>((target + 2) % kZoneCount), t, t);
  TEST_ASSERT_EQUAL_INT32(-2, engine->snapshot(t).players[0].score);
}

void test_target_times_out_as_miss() {
  uint32_t t = startAndLightBoth(soloRequest());
  const uint32_t active = engine->settings().difficulty[1].activeMs;
  runUntil(t, t + active + 10);
  const auto s = engine->snapshot(t + active + 10);
  TEST_ASSERT_TRUE(s.players[0].misses >= 1);
  bool sawMiss = false;
  for (const auto& e : listener->events) sawMiss |= e.kind == EventKind::Miss;
  TEST_ASSERT_TRUE(sawMiss);
}

void test_late_hit_inside_grace_converts_miss() {
  uint32_t t = startAndLightBoth(soloRequest());
  const uint8_t zone = static_cast<uint8_t>(engine->snapshot(t).lanes[0].target);
  const uint32_t expiry = t + engine->settings().difficulty[1].activeMs;
  // Expire only lane 0's target by ticking exactly to its deadline.
  engine->tick(expiry);
  auto s = engine->snapshot(expiry);
  const uint16_t missesBefore = s.players[0].misses;
  TEST_ASSERT_TRUE(missesBefore >= 1);

  engine->onHit(0, zone, expiry + 100, expiry + 120);

  s = engine->snapshot(expiry + 120);
  TEST_ASSERT_EQUAL_UINT16(missesBefore - 1, s.players[0].misses);
  TEST_ASSERT_EQUAL_UINT16(1, s.players[0].hits);
  TEST_ASSERT_TRUE(listener->events.back().late);
}

void test_hit_after_grace_counts_as_wrong() {
  uint32_t t = startAndLightBoth(soloRequest());
  const uint8_t zone = static_cast<uint8_t>(engine->snapshot(t).lanes[0].target);
  const uint32_t expiry = t + engine->settings().difficulty[1].activeMs;
  engine->tick(expiry);
  engine->onHit(0, zone, expiry + 400, expiry + 400);
  const auto s = engine->snapshot(expiry + 400);
  TEST_ASSERT_EQUAL_UINT16(0, s.players[0].hits);
  TEST_ASSERT_EQUAL_UINT16(1, s.players[0].wrong);
}

void test_burst_follow_up_on_scored_zone_is_ignored() {
  uint32_t t = startAndLightBoth(soloRequest());
  const uint8_t zone = static_cast<uint8_t>(engine->snapshot(t).lanes[0].target);
  engine->onHit(0, zone, t + 200, t + 200);
  engine->onHit(0, zone, t + 260, t + 260);
  engine->onHit(0, zone, t + 320, t + 320);
  const auto s = engine->snapshot(t + 320);
  TEST_ASSERT_EQUAL_UINT16(1, s.players[0].hits);
  TEST_ASSERT_EQUAL_UINT16(0, s.players[0].wrong);
}

void test_next_target_never_repeats_previous_zone() {
  uint32_t t = startAndLightBoth(soloRequest(120));
  int8_t last = engine->snapshot(t).lanes[0].target;
  int changes = 0;
  for (uint32_t now = t; now < t + 100000 && changes < 40; now += 10) {
    engine->tick(now);
    const auto lane = engine->snapshot(now).lanes[0];
    if (lane.phase == LanePhase::Active && lane.target != last && lane.target >= 0) {
      TEST_ASSERT_NOT_EQUAL(last, lane.target);
      last = lane.target;
      ++changes;
    }
    if (lane.phase == LanePhase::Active && lane.target >= 0) {
      engine->onHit(0, static_cast<uint8_t>(lane.target), now, now);
    }
  }
  TEST_ASSERT_TRUE(changes >= 20);
}

void test_zone_mask_limits_targets() {
  StartRequest r = soloRequest(120);
  r.zoneMask[0] = 0b00101;  // zones 0 and 2 only
  uint32_t t = startAndLightBoth(r);
  for (uint32_t now = t; now < t + 60000; now += 10) {
    engine->tick(now);
    const auto lane = engine->snapshot(now).lanes[0];
    if (lane.phase == LanePhase::Active) {
      TEST_ASSERT_TRUE(lane.target == 0 || lane.target == 2);
    }
  }
}

void test_duel_lanes_share_target_sequence() {
  uint32_t t = startAndLightBoth(duelRequest());
  std::vector<int8_t> seq[2];
  for (uint32_t now = t; now < t + 60000; now += 10) {
    engine->tick(now);
    const auto snap = engine->snapshot(now);
    for (uint8_t l = 0; l < 2; ++l) {
      const auto& lane = snap.lanes[l];
      if (lane.phase == LanePhase::Active && (seq[l].empty() || seq[l].back() != lane.target)) {
        seq[l].push_back(lane.target);
        // Lane 1 reacts slower than lane 0, so the lanes drift apart in time.
        engine->onHit(l, static_cast<uint8_t>(lane.target), now + (l == 0 ? 0 : 300), now + (l == 0 ? 0 : 300));
      }
    }
  }
  const size_t n = std::min(seq[0].size(), seq[1].size());
  TEST_ASSERT_TRUE(n >= 10);
  for (size_t i = 0; i < n; ++i) TEST_ASSERT_EQUAL_INT8(seq[0][i], seq[1][i]);
}

void test_duel_scores_go_to_lane_owner_and_winner_is_decided() {
  uint32_t t = startAndLightBoth(duelRequest());
  const auto s = engine->snapshot(t);
  engine->onHit(1, static_cast<uint8_t>(s.lanes[1].target), t + 100, t + 100);
  TEST_ASSERT_EQUAL_INT32(0, engine->snapshot(t + 100).players[0].score);
  TEST_ASSERT_TRUE(engine->snapshot(t + 100).players[1].score > 0);

  runUntil(t, kCountdown + 30000 + 10);
  TEST_ASSERT_EQUAL(static_cast<int>(GameState::Finished), static_cast<int>(engine->state()));
  TEST_ASSERT_EQUAL(1, static_cast<int>(listener->results.size()));
  TEST_ASSERT_EQUAL_INT8(1, listener->results[0].winner);
  TEST_ASSERT_EQUAL(static_cast<int>(EndReason::Finished), static_cast<int>(listener->results[0].reason));
}

void test_solo_uses_both_lanes_for_one_player() {
  uint32_t t = startAndLightBoth(soloRequest());
  const auto s = engine->snapshot(t);
  engine->onHit(0, static_cast<uint8_t>(s.lanes[0].target), t + 50, t + 50);
  engine->onHit(1, static_cast<uint8_t>(s.lanes[1].target), t + 60, t + 60);
  TEST_ASSERT_EQUAL_UINT16(2, engine->snapshot(t + 60).players[0].hits);
  TEST_ASSERT_EQUAL_UINT8(1, engine->snapshot(t + 60).playerCount);
}

void test_pause_freezes_clock_and_resume_relights_target() {
  uint32_t t = startAndLightBoth(soloRequest());
  const uint32_t remaining = engine->snapshot(t).remainingMs;
  const int8_t target = engine->snapshot(t).lanes[0].target;

  engine->pause(t);
  runUntil(t, t + 5000);
  TEST_ASSERT_EQUAL_UINT32(remaining, engine->snapshot(t + 5000).remainingMs);
  engine->onHit(0, static_cast<uint8_t>(target), t + 5000, t + 5000);
  TEST_ASSERT_EQUAL_UINT16(0, engine->snapshot(t + 5000).players[0].hits);

  listener->leds.clear();
  TEST_ASSERT_TRUE(engine->resume(t + 5000));
  TEST_ASSERT_EQUAL_UINT32(remaining, engine->snapshot(t + 5000).remainingMs);
  bool relit = false;
  for (const auto& c : listener->leds) {
    relit |= c.lane == 0 && c.zoneMask == (1u << target) && c.steady.color == LedColor::Green;
  }
  TEST_ASSERT_TRUE(relit);
}

void test_stop_ends_game_as_stopped_then_resets() {
  startAndLightBoth(soloRequest());
  engine->stop(5000);
  TEST_ASSERT_EQUAL(static_cast<int>(GameState::Finished), static_cast<int>(engine->state()));
  TEST_ASSERT_EQUAL(static_cast<int>(EndReason::Stopped), static_cast<int>(listener->results.back().reason));
  engine->stop(5100);
  TEST_ASSERT_EQUAL(static_cast<int>(GameState::Idle), static_cast<int>(engine->state()));
}

void test_hits_ignored_when_not_running() {
  engine->onHit(0, 1, 0, 0);
  engine->start(soloRequest(), 0);
  engine->onHit(0, 1, 100, 100);  // still counting down
  TEST_ASSERT_EQUAL_UINT16(0, engine->snapshot(100).players[0].wrong);
  TEST_ASSERT_TRUE(listener->events.empty());
}

void test_settings_locked_during_game_and_validated() {
  GameSettings bad;
  bad.difficulty[0].gapMinMs = 5000;
  bad.difficulty[0].gapMaxMs = 100;
  TEST_ASSERT_FALSE(engine->setSettings(bad));
  engine->start(soloRequest(), 0);
  TEST_ASSERT_FALSE(engine->setSettings(GameSettings{}));
}

void test_gap_between_targets_is_at_least_one_second() {
  for (const auto& d : engine->settings().difficulty) TEST_ASSERT_TRUE(d.gapMinMs >= kMinGapMs);
  GameSettings s;
  s.difficulty[2].gapMinMs = 500;
  TEST_ASSERT_FALSE(engine->setSettings(s));

  // Measure real gaps on the hardest level: target off -> next target lit.
  StartRequest r = soloRequest(120);
  r.difficulty = Difficulty::Hard;
  uint32_t t = startAndLightBoth(r);
  uint32_t offAt = 0;
  int gaps = 0;
  for (uint32_t now = t; now < t + 60000; now += 10) {
    engine->tick(now);
    const auto lane = engine->snapshot(now).lanes[0];
    if (lane.phase == LanePhase::Gap && offAt == 0) offAt = now;
    if (lane.phase == LanePhase::Active && offAt != 0) {
      TEST_ASSERT_TRUE(now - offAt >= kMinGapMs);
      offAt = 0;
      ++gaps;
    }
  }
  TEST_ASSERT_TRUE(gaps > 10);
}

void test_accuracy_and_average_reaction_time() {
  PlayerStats p;
  p.hits = 3;
  p.wrong = 1;
  p.rtSumMs = 1500;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, p.accuracy());
  TEST_ASSERT_EQUAL_UINT32(500, p.rtAvgMs());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rejects_invalid_requests);
  RUN_TEST(test_rejects_start_while_active);
  RUN_TEST(test_countdown_then_running_with_blinking_lights);
  RUN_TEST(test_hit_on_lit_zone_scores_zone_points);
  RUN_TEST(test_hit_on_unlit_zone_is_wrong_and_scores_nothing);
  RUN_TEST(test_wrong_penalty_is_applied_when_configured);
  RUN_TEST(test_target_times_out_as_miss);
  RUN_TEST(test_late_hit_inside_grace_converts_miss);
  RUN_TEST(test_hit_after_grace_counts_as_wrong);
  RUN_TEST(test_burst_follow_up_on_scored_zone_is_ignored);
  RUN_TEST(test_next_target_never_repeats_previous_zone);
  RUN_TEST(test_zone_mask_limits_targets);
  RUN_TEST(test_duel_lanes_share_target_sequence);
  RUN_TEST(test_duel_scores_go_to_lane_owner_and_winner_is_decided);
  RUN_TEST(test_solo_uses_both_lanes_for_one_player);
  RUN_TEST(test_pause_freezes_clock_and_resume_relights_target);
  RUN_TEST(test_stop_ends_game_as_stopped_then_resets);
  RUN_TEST(test_hits_ignored_when_not_running);
  RUN_TEST(test_settings_locked_during_game_and_validated);
  RUN_TEST(test_gap_between_targets_is_at_least_one_second);
  RUN_TEST(test_accuracy_and_average_reaction_time);
  return UNITY_END();
}
