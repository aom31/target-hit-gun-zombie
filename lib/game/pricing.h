#pragma once

// What a game costs: a price table of gun x play time, plus a fixed-price trial.
// The hub owns the table (admin page) and prices every game itself, so a phone can
// never send its own price. No Arduino dependencies.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "game_types.h"

namespace tr::game {

enum class GunKind : uint8_t { Long = 0, Short = 1, Electric = 2 };

constexpr uint8_t kMaxGuns = 12;
constexpr uint8_t kMaxDurations = 6;
constexpr uint32_t kMaxPrice = 100000;  // baht
constexpr size_t kMaxGunNameBytes = 48;  // UTF-8 (Thai is 3 bytes per character)
constexpr size_t kMaxGunNoteBytes = 90;

struct Gun {
  std::string name;               // e.g. "AR GBB"
  GunKind kind = GunKind::Long;
  std::string note;               // e.g. "ต้องเปลี่ยนแม็ก" for gas blowback
  std::vector<uint32_t> prices;   // baht, one per PriceList::durationsS entry
};

struct PriceList {
  bool enabled = false;  // on: show prices and need a staff PIN (payment received) to start
  std::vector<uint16_t> durationsS{30, 60, 90, 120};  // play times on offer, ascending
  std::vector<Gun> guns;
  uint16_t trialDurationS = 30;  // trial package: same price whatever the gun
  uint32_t trialPrice = 50;

  // nullptr when valid, else why not (Thai, shown on the admin page).
  const char* validate() const;
  int durationIndex(uint16_t seconds) const;  // -1 if not on offer
};

// The three example guns from the customer brief; prices start at 0 for the admin to fill.
PriceList defaultPriceList();

enum class QuoteError : uint8_t { None, BadDuration, NoGun, BadGun };

struct QuoteRequest {
  uint16_t durationS = 0;  // ignored for a trial
  bool trial = false;
  uint8_t players = 1;
  std::array<int, kMaxPlayers> gun{{-1, -1}};  // index into PriceList::guns, -1 = none
};

struct Quote {
  QuoteError error = QuoteError::None;
  uint16_t durationS = 0;  // what the game will actually run for
  uint32_t total = 0;
  std::array<uint32_t, kMaxPlayers> price{};
};

// Each player pays for their own gun. With guns configured, every player must pick
// one (also in a trial, so the history shows what was used).
Quote quote(const PriceList& list, const QuoteRequest& req);

const char* toString(GunKind k);
bool parseGunKind(const char* s, GunKind& out);

}  // namespace tr::game
