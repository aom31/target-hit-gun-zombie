#pragma once

// Rules of the game, independent of hardware and time source.
//
// The hub feeds it start/stop commands, hits and a periodic tick, all stamped
// with millis(); the engine answers through GameListener. Each mannequin is a
// "lane" with its own target cycle: GAP -> ACTIVE -> FEEDBACK -> GAP.

#include <array>

#include "game_types.h"
#include "xorshift.h"

namespace tr::game {

class GameEngine {
 public:
  explicit GameEngine(GameListener& listener);

  // Settings can only change while no game is in progress.
  bool setSettings(const GameSettings& settings);
  const GameSettings& settings() const { return settings_; }

  StartError start(const StartRequest& request, uint32_t nowMs);
  void stop(uint32_t nowMs);
  void pause(uint32_t nowMs);
  bool resume(uint32_t nowMs);
  void reset();  // Finished -> Idle

  // hitMs is when the node detected the hit (receive time minus reported age).
  void onHit(uint8_t lane, uint8_t zone, uint32_t hitMs, uint32_t nowMs);
  void tick(uint32_t nowMs);

  GameState state() const { return state_; }
  bool isActive() const;  // countdown, running or paused
  bool laneInUse(uint8_t lane) const { return lane < kLaneCount && lanes_[lane].inUse; }
  uint32_t revision() const { return revision_; }
  GameSnapshot snapshot(uint32_t nowMs) const;
  const GameResult& lastResult() const { return lastResult_; }

 private:
  struct PreviousTarget {
    bool valid = false;
    uint8_t zone = 0;
    uint32_t litAt = 0;
    uint32_t changedAt = 0;
    bool wasMiss = false;
  };

  struct Lane {
    bool inUse = false;
    uint8_t player = 0;
    uint8_t zoneMask = kAllZonesMask;
    LanePhase phase = LanePhase::Inactive;
    int8_t target = -1;
    uint32_t litAt = 0;
    uint32_t phaseEndsAt = 0;
    PreviousTarget prev;
    Xorshift32 rng;
  };

  void enterGap(uint8_t lane, uint32_t nowMs);
  void enterActive(uint8_t lane, uint32_t nowMs);
  void expireTarget(uint8_t lane, uint32_t nowMs);
  void scoreHit(uint8_t lane, uint8_t zone, uint32_t rtMs, bool late);
  void scoreWrong(uint8_t lane, uint8_t zone);
  void finish(EndReason reason, uint32_t nowMs);
  void allLanesLeds(const LedSteady& steady, LedColor flash = LedColor::Off, uint16_t flashMs = 0);
  void laneZoneOff(uint8_t lane, uint8_t zoneMask, LedColor flash, uint32_t flashMs);
  const DifficultyTiming& timing() const { return settings_.difficulty[static_cast<uint8_t>(difficulty_)]; }
  void bump() { ++revision_; }

  GameListener& listener_;
  GameSettings settings_;
  GameState state_ = GameState::Idle;
  GameMode mode_ = GameMode::Solo;
  Difficulty difficulty_ = Difficulty::Normal;
  uint16_t durationS_ = 0;
  uint32_t countdownEndsAt_ = 0;
  uint32_t endsAt_ = 0;
  uint32_t pausedAt_ = 0;
  uint8_t playerCount_ = 0;
  std::array<PlayerStats, kMaxPlayers> players_;
  std::array<Lane, kLaneCount> lanes_;
  GameResult lastResult_;
  uint32_t revision_ = 0;
};

// Wrap-safe "a is at or after b" for millis() timestamps.
inline bool reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

}  // namespace tr::game
