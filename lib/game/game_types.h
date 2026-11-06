#pragma once

// Plain data types for the game engine. No Arduino dependencies.

#include <array>
#include <cstdint>
#include <string>

#include "led_types.h"
#include "system_config.h"

namespace tr::game {

enum class GameState : uint8_t { Idle, Countdown, Running, Paused, Finished };
enum class GameMode : uint8_t { Solo = 1, Duel = 2 };
enum class Difficulty : uint8_t { Easy = 0, Normal = 1, Hard = 2 };
enum class LanePhase : uint8_t { Inactive, Gap, Active, FeedbackHit, FeedbackMiss };
enum class EventKind : uint8_t { Hit, Miss, Wrong };
enum class EndReason : uint8_t { Finished, Stopped };

constexpr uint8_t kDifficultyCount = 3;
constexpr uint8_t kMaxPlayers = 2;
constexpr size_t kMaxNameBytes = 48;  // UTF-8 bytes (Thai is 3 bytes per character)
constexpr uint16_t kMinDurationS = 10;
constexpr uint16_t kMaxDurationS = 600;
// Players need at least a second between targets to re-aim; shorter gaps feel unfair.
constexpr uint32_t kMinGapMs = 1000;

struct DifficultyTiming {
  uint32_t activeMs;  // how long a target stays lit
  uint32_t gapMinMs;  // random pause before the next target
  uint32_t gapMaxMs;
};

struct GameSettings {
  std::array<uint8_t, kZoneCount> zonePoints{{5, 2, 3, 4, 4}};
  uint8_t wrongPenalty = 0;
  std::array<DifficultyTiming, kDifficultyCount> difficulty{{
      {4000, 1500, 2500},
      {2500, 1000, 1800},
      {1500, 1000, 1400},
  }};
  uint32_t countdownMs = 5000;  // matches the length of the phone's countdown sound
  uint32_t graceMs = 150;
  uint32_t feedbackHitMs = 300;
  uint32_t feedbackMissMs = 150;
  uint32_t wrongFlashMs = 200;
  uint32_t finishFlashMs = 1500;

  bool isValid() const {
    for (uint8_t p : zonePoints) {
      if (p > 100) return false;
    }
    if (wrongPenalty > 100) return false;
    for (const auto& d : difficulty) {
      if (d.activeMs < 300 || d.activeMs > 20000) return false;
      if (d.gapMinMs < kMinGapMs || d.gapMinMs > d.gapMaxMs || d.gapMaxMs > 10000) return false;
    }
    return graceMs <= 1000 && countdownMs <= 10000;
  }
};

struct StartRequest {
  GameMode mode = GameMode::Solo;
  Difficulty difficulty = Difficulty::Normal;
  uint16_t durationS = 60;
  std::array<std::string, kMaxPlayers> names;
  std::array<uint8_t, kLaneCount> zoneMask{{kAllZonesMask, kAllZonesMask}};  // zones allowed per lane
  uint32_t seed = 1;
};

enum class StartError : uint8_t { None, Busy, BadMode, BadDuration, BadDifficulty, BadName, NoZones };

struct PlayerStats {
  std::string name;
  int32_t score = 0;
  uint16_t hits = 0;
  uint16_t misses = 0;
  uint16_t wrong = 0;
  uint32_t rtSumMs = 0;
  uint32_t rtBestMs = 0;
  std::array<uint16_t, kZoneCount> zoneHits{};
  std::array<int32_t, kZoneCount> zonePoints{};

  // Share of shots that landed on the lit zone.
  float accuracy() const {
    const uint32_t shots = static_cast<uint32_t>(hits) + wrong;
    return shots == 0 ? 0.0f : static_cast<float>(hits) / static_cast<float>(shots);
  }
  uint32_t rtAvgMs() const { return hits == 0 ? 0 : rtSumMs / hits; }
};

struct LaneView {
  LanePhase phase = LanePhase::Inactive;
  int8_t target = -1;
  uint8_t player = 0;
};

struct GameSnapshot {
  GameState state = GameState::Idle;
  GameMode mode = GameMode::Solo;
  Difficulty difficulty = Difficulty::Normal;
  uint16_t durationS = 0;
  uint32_t countdownEndsAt = 0;
  uint32_t endsAt = 0;
  uint32_t remainingMs = 0;
  uint8_t playerCount = 0;
  std::array<PlayerStats, kMaxPlayers> players;
  std::array<LaneView, kLaneCount> lanes;
  uint32_t revision = 0;
};

struct GameEvent {
  EventKind kind = EventKind::Hit;
  uint8_t lane = 0;
  uint8_t zone = 0;
  uint8_t player = 0;
  int16_t points = 0;
  uint32_t rtMs = 0;
  bool late = false;  // accepted inside the grace window after the target went out
};

struct GameResult {
  GameMode mode = GameMode::Solo;
  Difficulty difficulty = Difficulty::Normal;
  uint16_t durationS = 0;
  EndReason reason = EndReason::Finished;
  uint8_t playerCount = 0;
  std::array<PlayerStats, kMaxPlayers> players;
  int8_t winner = -1;  // duel only; -1 for solo or a draw
};

// Implemented by the hub to turn engine decisions into radio commands and UI messages.
class GameListener {
 public:
  virtual ~GameListener() = default;
  virtual void setLaneLeds(uint8_t lane, uint8_t zoneMask, const LedSteady& steady, LedColor flashColor,
                           uint16_t flashMs) = 0;
  virtual void onGameEvent(const GameEvent& event) = 0;
  virtual void onGameFinished(const GameResult& result) = 0;
};

const char* toString(GameState s);
const char* toString(GameMode m);
const char* toString(Difficulty d);
const char* toString(LanePhase p);
const char* toString(EventKind k);
const char* toString(EndReason r);
const char* toString(StartError e);

}  // namespace tr::game
