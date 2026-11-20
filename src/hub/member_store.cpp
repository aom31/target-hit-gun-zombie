#include "member_store.h"

#include <esp_random.h>
#include <mbedtls/md.h>

#include "storage.h"

namespace tr::hub {

namespace {

constexpr const char* kTempPath = "/members.tmp";

String toHex(const uint8_t* data, size_t len) {
  static const char* digits = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += digits[data[i] >> 4];
    out += digits[data[i] & 0x0F];
  }
  return out;
}

// HMAC-SHA256 when key is given, plain SHA-256 otherwise.
String digest(const String& key, const String& message) {
  uint8_t out[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (key.length() > 0) {
    mbedtls_md_hmac(info, reinterpret_cast<const uint8_t*>(key.c_str()), key.length(),
                    reinterpret_cast<const uint8_t*>(message.c_str()), message.length(), out);
  } else {
    mbedtls_md(info, reinterpret_cast<const uint8_t*>(message.c_str()), message.length(), out);
  }
  return toHex(out, sizeof(out));
}

}  // namespace

MemberStore::MemberStore() : book_(&MemberStore::hashPin) {}

std::string MemberStore::hashPin(const std::string& salt, const std::string& pin) {
  return digest(String(), String(salt.c_str()) + ":" + pin.c_str()).c_str();
}

String MemberStore::randomHex(size_t bytes) {
  uint8_t buf[32];
  if (bytes > sizeof(buf)) bytes = sizeof(buf);
  esp_fill_random(buf, bytes);
  return toHex(buf, bytes);
}

String MemberStore::tokenFor(const game::Member& m) const {
  return digest(secret_, String(m.id) + ":" + m.pinVer).substring(0, 32);
}

const game::Member* MemberStore::fromToken(uint32_t id, const char* token) const {
  const game::Member* m = book_.find(id);
  if (m == nullptr || token == nullptr || secret_.length() == 0) return nullptr;
  return tokenFor(*m) == token ? m : nullptr;
}

void MemberStore::toJson(JsonArray out) const {
  for (const game::Member& m : book_.all()) {
    JsonObject o = out.add<JsonObject>();
    o["id"] = m.id;
    o["name"] = m.name.c_str();
    o["key"] = m.key.c_str();
    o["salt"] = m.salt.c_str();
    o["pin"] = m.pinHash.c_str();
    o["v"] = m.pinVer;
    if (!m.last4.empty()) o["last4"] = m.last4.c_str();
    o["ts"] = m.createdTs;
  }
}

bool MemberStore::replaceFrom(JsonArrayConst in) {
  if (in.isNull()) return false;
  std::vector<game::Member> list;
  for (JsonObjectConst o : in) {
    game::Member m;
    m.id = o["id"] | 0UL;
    m.name = String(o["name"] | "").c_str();
    m.key = String(o["key"] | "").c_str();
    m.salt = String(o["salt"] | "").c_str();
    m.pinHash = String(o["pin"] | "").c_str();
    m.pinVer = o["v"] | 1UL;
    m.last4 = String(o["last4"] | "").c_str();
    m.createdTs = o["ts"] | 0ULL;
    if (m.id == 0 || m.name.empty() || m.pinHash.empty()) continue;  // damaged entry
    list.push_back(m);
  }
  book_.load(std::move(list));
  return true;
}

bool MemberStore::load() {
  File f = storage::data().open(kPath, "r");
  if (!f) return false;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  return replaceFrom(doc["members"].as<JsonArrayConst>());
}

bool MemberStore::save() const {
  JsonDocument doc;
  toJson(doc["members"].to<JsonArray>());
  // Write to a temp file then rename, so a power cut never leaves a half-written file.
  File f = storage::data().open(kTempPath, "w");
  if (!f) return false;
  const bool ok = serializeJson(doc, f) > 0;
  f.close();
  if (!ok) return false;
  return storage::replaceFile(kTempPath, kPath);
}

}  // namespace tr::hub
