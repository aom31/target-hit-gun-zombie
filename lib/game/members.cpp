#include "members.h"

#include <algorithm>

namespace tr::game {

namespace {

constexpr size_t kMaxLimiterEntries = 32;

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Trims and collapses whitespace; drops ASCII control characters.
std::string tidy(const std::string& in) {
  std::string out;
  bool pendingSpace = false;
  for (char c : in) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (isSpace(c)) {
      pendingSpace = !out.empty();
      continue;
    }
    if (u < 0x20 || u == 0x7F) continue;
    if (pendingSpace) out += ' ';
    pendingSpace = false;
    out += c;
  }
  return out;
}

bool allDigits(const std::string& s) {
  return std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

std::string cleanMemberName(const std::string& name) { return tidy(name); }

std::string nameKey(const std::string& name) {
  std::string k = tidy(name);
  for (char& c : k) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return k;
}

bool validPin(const std::string& pin) { return pin.size() == 4 && allDigits(pin); }

bool validLast4(const std::string& s) { return s.empty() || (s.size() == 4 && allDigits(s)); }

MemberError MemberBook::add(const std::string& name, const std::string& pin, const std::string& last4,
                            const std::string& salt, const std::string& key, uint64_t createdTs, uint32_t& newId) {
  const std::string clean = cleanMemberName(name);
  if (clean.empty() || clean.size() > kMaxMemberNameBytes) return MemberError::BadName;
  if (!validPin(pin)) return MemberError::BadPin;
  if (!validLast4(last4)) return MemberError::BadLast4;
  if (findByName(clean) != nullptr) return MemberError::NameTaken;
  if (members_.size() >= kMaxMembers) return MemberError::Full;
  Member m;
  m.id = nextId_++;
  m.name = clean;
  m.key = key;
  m.salt = salt;
  m.pinHash = hash_(salt, pin);
  m.last4 = last4;
  m.createdTs = createdTs;
  members_.push_back(m);
  newId = m.id;
  return MemberError::None;
}

bool MemberBook::checkPin(const Member& m, const std::string& pin) const {
  return validPin(pin) && hash_(m.salt, pin) == m.pinHash;
}

MemberError MemberBook::resetPin(uint32_t id, const std::string& pin, const std::string& newSalt) {
  Member* m = find(id);
  if (m == nullptr) return MemberError::NotFound;
  if (!validPin(pin)) return MemberError::BadPin;
  m->salt = newSalt;
  m->pinHash = hash_(newSalt, pin);
  ++m->pinVer;
  return MemberError::None;
}

bool MemberBook::remove(uint32_t id) {
  const auto it = std::find_if(members_.begin(), members_.end(), [id](const Member& m) { return m.id == id; });
  if (it == members_.end()) return false;
  members_.erase(it);
  return true;
}

Member* MemberBook::find(uint32_t id) {
  for (Member& m : members_) {
    if (m.id == id) return &m;
  }
  return nullptr;
}

const Member* MemberBook::find(uint32_t id) const { return const_cast<MemberBook*>(this)->find(id); }

Member* MemberBook::findByName(const std::string& name) {
  const std::string k = nameKey(name);
  if (k.empty()) return nullptr;
  for (Member& m : members_) {
    if (nameKey(m.name) == k) return &m;
  }
  return nullptr;
}

const Member* MemberBook::findByName(const std::string& name) const {
  return const_cast<MemberBook*>(this)->findByName(name);
}

std::vector<const Member*> MemberBook::search(const std::string& query, size_t limit) const {
  std::vector<const Member*> out;
  const std::string q = nameKey(query);
  for (const Member& m : members_) {
    if (out.size() >= limit) break;
    if (q.empty() || nameKey(m.name).find(q) != std::string::npos) out.push_back(&m);
  }
  return out;
}

void MemberBook::load(std::vector<Member> members) {
  members_ = std::move(members);
  nextId_ = 1;
  for (const Member& m : members_) nextId_ = std::max(nextId_, m.id + 1);
}

// ---------------------------------------------------------------- login limiter

LoginLimiter::Entry* LoginLimiter::get(const std::string& key) {
  for (Entry& e : entries_) {
    if (e.key == key) return &e;
  }
  return nullptr;
}

bool LoginLimiter::locked(const std::string& key, uint32_t nowMs) const {
  for (const Entry& e : entries_) {
    if (e.key == key) return e.lockedNow && static_cast<int32_t>(e.lockedUntil - nowMs) > 0;
  }
  return false;
}

void LoginLimiter::fail(const std::string& key, uint32_t nowMs) {
  Entry* e = get(key);
  if (e == nullptr) {
    if (entries_.size() >= kMaxLimiterEntries) {
      const auto oldest = std::min_element(entries_.begin(), entries_.end(),
                                           [](const Entry& a, const Entry& b) { return a.lastMs < b.lastMs; });
      entries_.erase(oldest);
    }
    entries_.push_back(Entry{key, 0, nowMs, 0, false});
    e = &entries_.back();
  }
  // A lock that has run out starts a fresh count.
  if (e->lockedNow && static_cast<int32_t>(e->lockedUntil - nowMs) <= 0) {
    e->lockedNow = false;
    e->fails = 0;
  }
  e->lastMs = nowMs;
  if (++e->fails >= kMaxLoginFails) {
    e->lockedNow = true;
    e->lockedUntil = nowMs + kLoginLockMs;
  }
}

void LoginLimiter::succeed(const std::string& key) {
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&key](const Entry& e) { return e.key == key; }),
                 entries_.end());
}

}  // namespace tr::game
