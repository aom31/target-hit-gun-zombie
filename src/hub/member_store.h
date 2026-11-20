#pragma once

// Members on the hub: /members.json on the data partition, PIN hashing (SHA-256)
// and the tokens phones keep instead of the PIN.
//
// token = first 32 hex chars of HMAC-SHA256(secret, "<id>:<pinVer>"). Nothing is kept
// per session; a PIN change bumps pinVer, which voids every token issued before.
// The rules (names, PINs, lockout) live in lib/game/members.h.

#include <Arduino.h>
#include <ArduinoJson.h>

#include "members.h"

namespace tr::hub {

class MemberStore {
 public:
  static constexpr const char* kPath = "/members.json";

  MemberStore();

  bool load();
  bool save() const;
  void setSecret(const String& secret) { secret_ = secret; }

  game::MemberBook& book() { return book_; }
  const game::MemberBook& book() const { return book_; }
  game::LoginLimiter& limiter() { return limiter_; }

  String tokenFor(const game::Member& m) const;
  // The member a phone's (id, token) belongs to, or nullptr if the token is stale.
  const game::Member* fromToken(uint32_t id, const char* token) const;

  // Backups carry the members file as a JSON array.
  void toJson(JsonArray out) const;
  bool replaceFrom(JsonArrayConst in);

  static String randomHex(size_t bytes);  // hardware RNG
  static std::string hashPin(const std::string& salt, const std::string& pin);

 private:
  game::MemberBook book_;
  game::LoginLimiter limiter_;
  String secret_;
};

}  // namespace tr::hub
