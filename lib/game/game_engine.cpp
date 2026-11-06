#include "game_engine.h"

namespace tr::game {

namespace {

constexpr LedSteady kOff{};
constexpr LedSteady kTargetLook{LedColor::Green, LedFx::Solid, 0};
constexpr LedSteady kCountdownLook{LedColor::White, LedFx::Blink, 500};
constexpr LedSteady kPausedLook{LedColor::Yellow, LedFx::Blink, 1000};

uint8_t zoneBit(uint8_t zone) { return static_cast<uint8_t>(1u << zone); }

bool validName(const std::string& name) { return !name.empty() && name.size() <= kMaxNameBytes; }

}  // namespace

GameEngine::GameEngine(GameListener& listener) : listener_(listener) {}

bool GameEngine::setSettings(const GameSettings& settings) {
  if (isActive() || !settings.isValid()) return false;
  settings_ = settings;
  bump();
  return true;
}

bool GameEngine::isActive() const {
  return state_ == GameState::Countdown || state_ == GameState::Running || state_ == GameState::Paused;
}

StartError GameEngine::start(const StartRequest& req, uint32_t nowMs) {
  if (isActive()) return StartError::Busy;
  if (req.mode != GameMode::Solo && req.mode != GameMode::Duel) return StartError::BadMode;
  if (req.durationS < kMinDurationS || req.durationS > kMaxDurationS) return StartError::BadDuration;
  if (static_cast<uint8_t>(req.difficulty) >= kDifficultyCount) return StartError::BadDifficulty;

  const uint8_t playerCount = req.mode == GameMode::Duel ? 2 : 1;
  for (uint8_t p = 0; p < playerCount; ++p) {
    if (!validName(req.names[p])) return StartError::BadName;
  }
  for (uint8_t mask : req.zoneMask) {
    if ((mask & kAllZonesMask) == 0) return StartError::NoZones;
  }

  mode_ = req.mode;
  difficulty_ = req.difficulty;
  durationS_ = req.durationS;
  playerCount_ = playerCount;
  for (uint8_t p = 0; p < kMaxPlayers; ++p) {
    players_[p] = PlayerStats{};
    if (p < playerCount) players_[p].name = req.names[p];
  }

  // Duel: identical seeds give both players the same target sequence.
  // Solo: different seeds so the two mannequins don't mirror each other.
  for (uint8_t l = 0; l < kLaneCount; ++l) {
    Lane& lane = lanes_[l];
    lane = Lane{};
    lane.inUse = true;
    lane.player = req.mode == GameMode::Duel ? l : 0;
    lane.zoneMask = req.zoneMask[l] & kAllZonesMask;
    const uint32_t seed = req.mode == GameMode::Duel ? req.seed : req.seed * 2654435761u + l * 40503u;
    lane.rng.reseed(seed);
  }

  state_ = GameState::Countdown;
  countdownEndsAt_ = nowMs + settings_.countdownMs;
  endsAt_ = 0;
  allLanesLeds(kCountdownLook);
  bump();
  return StartError::None;
}

void GameEngine::stop(uint32_t nowMs) {
  if (isActive()) {
    finish(EndReason::Stopped, nowMs);
  } else if (state_ == GameState::Finished) {
    reset();
  }
}

void GameEngine::reset() {
  if (isActive()) return;
  state_ = GameState::Idle;
  for (Lane& lane : lanes_) lane = Lane{};
  bump();
}

void GameEngine::pause(uint32_t nowMs) {
  if (state_ != GameState::Running && state_ != GameState::Countdown) return;
  // A countdown that gets interrupted simply restarts on resume.
  pausedAt_ = nowMs;
  state_ = GameState::Paused;
  allLanesLeds(kPausedLook);
  bump();
}

bool GameEngine::resume(uint32_t nowMs) {
  if (state_ != GameState::Paused) return false;
  if (endsAt_ == 0) {
    // Paused during countdown: count down again.
    state_ = GameState::Countdown;
    countdownEndsAt_ = nowMs + settings_.countdownMs;
    allLanesLeds(kCountdownLook);
    bump();
    return true;
  }

  const uint32_t shift = nowMs - pausedAt_;
  endsAt_ += shift;
  for (Lane& lane : lanes_) {
    if (!lane.inUse) continue;
    lane.phaseEndsAt += shift;
    lane.litAt += shift;
    lane.prev.changedAt += shift;
    lane.prev.litAt += shift;
  }
  state_ = GameState::Running;
  allLanesLeds(kOff);
  for (uint8_t l = 0; l < kLaneCount; ++l) {
    const Lane& lane = lanes_[l];
    if (lane.inUse && lane.phase == LanePhase::Active && lane.target >= 0) {
      listener_.setLaneLeds(l, zoneBit(static_cast<uint8_t>(lane.target)), kTargetLook, LedColor::Off, 0);
    }
  }
  bump();
  return true;
}

void GameEngine::tick(uint32_t nowMs) {
  switch (state_) {
    case GameState::Countdown:
      if (reached(nowMs, countdownEndsAt_)) {
        state_ = GameState::Running;
        endsAt_ = nowMs + static_cast<uint32_t>(durationS_) * 1000u;
        allLanesLeds(kOff);
        for (uint8_t l = 0; l < kLaneCount; ++l) {
          if (lanes_[l].inUse) enterGap(l, nowMs);
        }
        bump();
      }
      break;

    case GameState::Running:
      if (reached(nowMs, endsAt_)) {
        finish(EndReason::Finished, nowMs);
        return;
      }
      for (uint8_t l = 0; l < kLaneCount; ++l) {
        Lane& lane = lanes_[l];
        if (!lane.inUse || !reached(nowMs, lane.phaseEndsAt)) continue;
        switch (lane.phase) {
          case LanePhase::Gap:
            enterActive(l, nowMs);
            break;
          case LanePhase::Active:
            expireTarget(l, nowMs);
            break;
          case LanePhase::FeedbackHit:
          case LanePhase::FeedbackMiss:
            enterGap(l, nowMs);
            break;
          case LanePhase::Inactive:
            break;
        }
      }
      break;

    case GameState::Idle:
    case GameState::Paused:
    case GameState::Finished:
      break;
  }
}

void GameEngine::onHit(uint8_t laneIndex, uint8_t zone, uint32_t hitMs, uint32_t nowMs) {
  if (state_ != GameState::Running || laneIndex >= kLaneCount || zone >= kZoneCount) return;
  Lane& lane = lanes_[laneIndex];
  if (!lane.inUse) return;

  if (lane.phase == LanePhase::Active && zone == lane.target) {
    const uint32_t rt = static_cast<int32_t>(hitMs - lane.litAt) > 0 ? hitMs - lane.litAt : 0;
    scoreHit(laneIndex, zone, rt, false);
    lane.prev = PreviousTarget{true, zone, lane.litAt, nowMs, false};
    laneZoneOff(laneIndex, zoneBit(zone), LedColor::Blue, settings_.feedbackHitMs);
    lane.phase = LanePhase::FeedbackHit;
    lane.target = -1;
    lane.phaseEndsAt = nowMs + settings_.feedbackHitMs;
    bump();
    return;
  }

  PreviousTarget& prev = lane.prev;
  if (prev.valid && zone == prev.zone) {
    const int32_t sinceChange = static_cast<int32_t>(hitMs - prev.changedAt);
    if (prev.wasMiss && sinceChange <= static_cast<int32_t>(settings_.graceMs)) {
      // The BB was in flight when the target timed out: turn the miss into a hit.
      PlayerStats& stats = players_[lane.player];
      if (stats.misses > 0) --stats.misses;
      const uint32_t rt = static_cast<int32_t>(hitMs - prev.litAt) > 0 ? hitMs - prev.litAt : 0;
      scoreHit(laneIndex, zone, rt, true);
      prev.wasMiss = false;
      laneZoneOff(laneIndex, zoneBit(zone), LedColor::Blue, settings_.feedbackHitMs);
      bump();
      return;
    }
    if (!prev.wasMiss &&
        static_cast<int32_t>(nowMs - prev.changedAt) <=
            static_cast<int32_t>(settings_.feedbackHitMs + settings_.graceMs)) {
      // Follow-up rounds of a burst on a zone that was just scored: neither reward nor punish.
      return;
    }
  }

  scoreWrong(laneIndex, zone);
  laneZoneOff(laneIndex, zoneBit(zone), LedColor::Red, settings_.wrongFlashMs);
  bump();
}

GameSnapshot GameEngine::snapshot(uint32_t nowMs) const {
  GameSnapshot s;
  s.state = state_;
  s.mode = mode_;
  s.difficulty = difficulty_;
  s.durationS = durationS_;
  s.countdownEndsAt = countdownEndsAt_;
  s.endsAt = endsAt_;
  s.playerCount = playerCount_;
  s.players = players_;
  s.revision = revision_;

  switch (state_) {
    case GameState::Countdown:
      s.remainingMs = static_cast<uint32_t>(durationS_) * 1000u;
      break;
    case GameState::Running:
      s.remainingMs = reached(nowMs, endsAt_) ? 0 : endsAt_ - nowMs;
      break;
    case GameState::Paused:
      s.remainingMs = endsAt_ == 0 ? static_cast<uint32_t>(durationS_) * 1000u
                                   : (reached(pausedAt_, endsAt_) ? 0 : endsAt_ - pausedAt_);
      break;
    default:
      s.remainingMs = 0;
  }

  for (uint8_t l = 0; l < kLaneCount; ++l) {
    s.lanes[l].phase = lanes_[l].phase;
    s.lanes[l].target = lanes_[l].target;
    s.lanes[l].player = lanes_[l].player;
  }
  return s;
}

void GameEngine::enterGap(uint8_t l, uint32_t nowMs) {
  Lane& lane = lanes_[l];
  const DifficultyTiming& t = timing();
  lane.phase = LanePhase::Gap;
  lane.target = -1;
  lane.phaseEndsAt = nowMs + lane.rng.range(t.gapMinMs, t.gapMaxMs);
  bump();
}

void GameEngine::enterActive(uint8_t l, uint32_t nowMs) {
  Lane& lane = lanes_[l];

  uint8_t candidates[kZoneCount];
  uint8_t count = 0;
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    if ((lane.zoneMask & zoneBit(z)) == 0) continue;
    if (lane.prev.valid && lane.prev.zone == z) continue;
    candidates[count++] = z;
  }
  if (count == 0) {
    // Only one zone enabled: repeating it is the only option.
    for (uint8_t z = 0; z < kZoneCount; ++z) {
      if (lane.zoneMask & zoneBit(z)) candidates[count++] = z;
    }
  }

  const uint8_t zone = candidates[lane.rng.next() % count];
  lane.phase = LanePhase::Active;
  lane.target = static_cast<int8_t>(zone);
  lane.litAt = nowMs;
  lane.phaseEndsAt = nowMs + timing().activeMs;
  listener_.setLaneLeds(l, zoneBit(zone), kTargetLook, LedColor::Off, 0);
  bump();
}

void GameEngine::expireTarget(uint8_t l, uint32_t nowMs) {
  Lane& lane = lanes_[l];
  const uint8_t zone = static_cast<uint8_t>(lane.target);
  PlayerStats& stats = players_[lane.player];
  ++stats.misses;

  lane.prev = PreviousTarget{true, zone, lane.litAt, nowMs, true};
  laneZoneOff(l, zoneBit(zone), LedColor::Off, 0);
  lane.phase = LanePhase::FeedbackMiss;
  lane.target = -1;
  lane.phaseEndsAt = nowMs + settings_.feedbackMissMs;

  GameEvent ev;
  ev.kind = EventKind::Miss;
  ev.lane = l;
  ev.zone = zone;
  ev.player = lane.player;
  listener_.onGameEvent(ev);
  bump();
}

void GameEngine::scoreHit(uint8_t l, uint8_t zone, uint32_t rtMs, bool late) {
  const Lane& lane = lanes_[l];
  PlayerStats& stats = players_[lane.player];
  const int16_t points = settings_.zonePoints[zone];
  stats.score += points;
  ++stats.hits;
  ++stats.zoneHits[zone];
  stats.zonePoints[zone] += points;
  stats.rtSumMs += rtMs;
  if (stats.hits == 1 || rtMs < stats.rtBestMs) stats.rtBestMs = rtMs;

  GameEvent ev;
  ev.kind = EventKind::Hit;
  ev.lane = l;
  ev.zone = zone;
  ev.player = lane.player;
  ev.points = points;
  ev.rtMs = rtMs;
  ev.late = late;
  listener_.onGameEvent(ev);
}

void GameEngine::scoreWrong(uint8_t l, uint8_t zone) {
  const Lane& lane = lanes_[l];
  PlayerStats& stats = players_[lane.player];
  const int16_t penalty = settings_.wrongPenalty;
  stats.score -= penalty;
  ++stats.wrong;

  GameEvent ev;
  ev.kind = EventKind::Wrong;
  ev.lane = l;
  ev.zone = zone;
  ev.player = lane.player;
  ev.points = static_cast<int16_t>(-penalty);
  listener_.onGameEvent(ev);
}

void GameEngine::finish(EndReason reason, uint32_t nowMs) {
  (void)nowMs;
  if (reason == EndReason::Finished) {
    allLanesLeds(kOff, LedColor::White, static_cast<uint16_t>(settings_.finishFlashMs));
  } else {
    allLanesLeds(kOff);
  }
  for (Lane& lane : lanes_) {
    lane.phase = LanePhase::Inactive;
    lane.target = -1;
  }

  GameResult r;
  r.mode = mode_;
  r.difficulty = difficulty_;
  r.durationS = durationS_;
  r.reason = reason;
  r.playerCount = playerCount_;
  r.players = players_;
  r.winner = -1;
  if (mode_ == GameMode::Duel) {
    const PlayerStats& a = players_[0];
    const PlayerStats& b = players_[1];
    if (a.score != b.score) {
      r.winner = a.score > b.score ? 0 : 1;
    } else if (a.hits != b.hits) {
      r.winner = a.hits > b.hits ? 0 : 1;
    }
  }

  state_ = GameState::Finished;
  lastResult_ = r;
  bump();
  listener_.onGameFinished(r);
}

void GameEngine::allLanesLeds(const LedSteady& steady, LedColor flash, uint16_t flashMs) {
  for (uint8_t l = 0; l < kLaneCount; ++l) {
    if (lanes_[l].inUse) listener_.setLaneLeds(l, kAllZonesMask, steady, flash, flashMs);
  }
}

void GameEngine::laneZoneOff(uint8_t l, uint8_t zoneMask, LedColor flash, uint32_t flashMs) {
  listener_.setLaneLeds(l, zoneMask, kOff, flash, static_cast<uint16_t>(flashMs));
}

const char* toString(GameState s) {
  switch (s) {
    case GameState::Idle: return "idle";
    case GameState::Countdown: return "countdown";
    case GameState::Running: return "running";
    case GameState::Paused: return "paused";
    case GameState::Finished: return "finished";
  }
  return "?";
}

const char* toString(GameMode m) { return m == GameMode::Duel ? "duel" : "solo"; }

const char* toString(Difficulty d) {
  switch (d) {
    case Difficulty::Easy: return "easy";
    case Difficulty::Normal: return "normal";
    case Difficulty::Hard: return "hard";
  }
  return "?";
}

const char* toString(LanePhase p) {
  switch (p) {
    case LanePhase::Inactive: return "inactive";
    case LanePhase::Gap: return "gap";
    case LanePhase::Active: return "active";
    case LanePhase::FeedbackHit: return "feedback_hit";
    case LanePhase::FeedbackMiss: return "feedback_miss";
  }
  return "?";
}

const char* toString(EventKind k) {
  switch (k) {
    case EventKind::Hit: return "hit";
    case EventKind::Miss: return "miss";
    case EventKind::Wrong: return "wrong";
  }
  return "?";
}

const char* toString(EndReason r) { return r == EndReason::Stopped ? "stopped" : "finished"; }

const char* toString(StartError e) {
  switch (e) {
    case StartError::None: return "none";
    case StartError::Busy: return "busy";
    case StartError::BadMode: return "bad_mode";
    case StartError::BadDuration: return "bad_duration";
    case StartError::BadDifficulty: return "bad_difficulty";
    case StartError::BadName: return "bad_name";
    case StartError::NoZones: return "no_zones";
  }
  return "?";
}

}  // namespace tr::game
