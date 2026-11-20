#pragma once

// Members: a customer who signs up with a name + 4-digit PIN gets every game they
// play recorded under a member id, so they can see their whole history later on any
// phone. Guests still play under a typed name (but not a member's name).
//
// Only the hub checks PINs. Phones remember a member by a token the hub derives from
// a secret (see member_store.h), never by the PIN. No Arduino dependencies: the PIN
// hash function is injected (mbedTLS SHA-256 on the hub, a stand-in in tests).

#include <cstdint>
#include <string>
#include <vector>

namespace tr::game {

constexpr size_t kMaxMemberNameBytes = 48;  // UTF-8, as player names
constexpr size_t kMaxMembers = 5000;
constexpr uint8_t kMaxLoginFails = 5;
constexpr uint32_t kLoginLockMs = 5UL * 60UL * 1000UL;

// Comparison form of a name: trimmed, runs of spaces collapsed, A-Z lowered.
// "ต้น" and " ต้น  " are the same member name.
std::string nameKey(const std::string& name);
// Display form: trimmed, runs of spaces collapsed, control characters dropped.
std::string cleanMemberName(const std::string& name);
bool validPin(const std::string& pin);    // exactly 4 digits
bool validLast4(const std::string& s);    // empty, or exactly 4 digits

struct Member {
  uint32_t id = 0;
  std::string name;
  std::string key;      // 32 hex chars: the private "view from home" link (cloud /members/<key>)
  std::string salt;     // per-member salt for the PIN hash
  std::string pinHash;  // hash(salt, pin)
  std::string last4;    // last 4 digits of their phone, optional: staff check it before a PIN reset
  uint32_t pinVer = 1;  // bumped on every PIN change: tokens issued before stop working
  uint64_t createdTs = 0;
};

using PinHashFn = std::string (*)(const std::string& salt, const std::string& pin);

enum class MemberError : uint8_t { None, BadName, BadPin, BadLast4, NameTaken, Full, NotFound, WrongPin, Locked };

class MemberBook {
 public:
  explicit MemberBook(PinHashFn hash) : hash_(hash) {}

  // salt and key come from the caller (hardware RNG on the hub).
  MemberError add(const std::string& name, const std::string& pin, const std::string& last4, const std::string& salt,
                  const std::string& key, uint64_t createdTs, uint32_t& newId);
  bool checkPin(const Member& m, const std::string& pin) const;
  MemberError resetPin(uint32_t id, const std::string& pin, const std::string& newSalt);
  bool remove(uint32_t id);

  Member* find(uint32_t id);
  const Member* find(uint32_t id) const;
  Member* findByName(const std::string& name);
  const Member* findByName(const std::string& name) const;
  std::vector<const Member*> search(const std::string& query, size_t limit) const;  // name contains query

  const std::vector<Member>& all() const { return members_; }
  // Loading from storage: takes members as saved (no validation of the hash).
  void load(std::vector<Member> members);
  uint32_t nextId() const { return nextId_; }

 private:
  PinHashFn hash_;
  std::vector<Member> members_;
  uint32_t nextId_ = 1;
};

// Slows down PIN guessing: after kMaxLoginFails wrong PINs for a name, that name is
// locked for kLoginLockMs. Keeps a small table; the oldest entry makes room.
class LoginLimiter {
 public:
  bool locked(const std::string& key, uint32_t nowMs) const;
  void fail(const std::string& key, uint32_t nowMs);
  void succeed(const std::string& key);

 private:
  struct Entry {
    std::string key;
    uint8_t fails = 0;
    uint32_t lastMs = 0;
    uint32_t lockedUntil = 0;
    bool lockedNow = false;
  };
  Entry* get(const std::string& key);
  std::vector<Entry> entries_;
};

}  // namespace tr::game
