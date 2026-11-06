#include "hub_settings.h"

#include "storage.h"

namespace tr::hub {

namespace {
constexpr const char* kPath = "/settings.json";
constexpr const char* kTempPath = "/settings.tmp";
constexpr int kSchema = 1;
constexpr const char* kOrderNames[] = {"RGB", "GRB", "BRG", "RBG", "GBR", "BGR"};

template <size_t N, typename T>
bool readArray(JsonArrayConst in, std::array<T, N>& out, long lo, long hi) {
  if (in.isNull() || in.size() != N) return false;
  std::array<T, N> tmp{};
  for (size_t i = 0; i < N; ++i) {
    if (!in[i].is<long>()) return false;
    const long v = in[i].as<long>();
    if (v < lo || v > hi) return false;
    tmp[i] = static_cast<T>(v);
  }
  out = tmp;
  return true;
}

bool inRange(JsonVariantConst v, long lo, long hi) {
  return v.is<long>() && v.as<long>() >= lo && v.as<long>() <= hi;
}
}  // namespace

const char* colorOrderName(ColorOrder o) {
  const uint8_t i = static_cast<uint8_t>(o);
  return i < static_cast<uint8_t>(ColorOrder::Count) ? kOrderNames[i] : "RGB";
}

bool parseColorOrder(const char* s, ColorOrder& out) {
  if (s == nullptr) return false;
  for (uint8_t i = 0; i < static_cast<uint8_t>(ColorOrder::Count); ++i) {
    if (strcmp(s, kOrderNames[i]) == 0) {
      out = static_cast<ColorOrder>(i);
      return true;
    }
  }
  return false;
}

void HubSettings::toJson(JsonObject out, bool includeSecrets) const {
  out["schema"] = kSchema;

  JsonObject w = out["wifi"].to<JsonObject>();
  w["ssid"] = wifi.ssid;
  w["channel"] = wifi.channel;
  if (includeSecrets) {
    w["password"] = wifi.password;
    out["admin_pin"] = adminPin;
  }

  JsonObject c = out["cloud"].to<JsonObject>();
  c["enabled"] = cloud.enabled;
  c["ssid"] = cloud.ssid;
  c["email"] = cloud.email;
  if (includeSecrets) {
    c["password"] = cloud.password;
    c["secret"] = cloud.secret;
  } else {
    c["has_password"] = cloud.password.length() > 0;
    c["has_secret"] = cloud.secret.length() > 0;
  }

  JsonObject g = out["game"].to<JsonObject>();
  JsonArray pts = g["zone_points"].to<JsonArray>();
  for (uint8_t p : game.zonePoints) pts.add(p);
  g["wrong_penalty"] = game.wrongPenalty;
  g["grace_ms"] = game.graceMs;
  JsonArray diff = g["difficulty"].to<JsonArray>();
  for (const auto& d : game.difficulty) {
    JsonObject o = diff.add<JsonObject>();
    o["active_ms"] = d.activeMs;
    o["gap_min_ms"] = d.gapMinMs;
    o["gap_max_ms"] = d.gapMaxMs;
  }

  JsonObject h = out["hardware"].to<JsonObject>();
  h["lockout_ms"] = hardware.lockoutMs;
  h["arbiter_ms"] = hardware.arbiterWindowMs;
  h["brightness"] = hardware.brightness;
  h["color_order"] = colorOrderName(hardware.colorOrder);
  h["led_khz"] = hardware.ledProtocol == LedProtocol::Khz400 ? 400 : 800;
  h["volume"] = hardware.volume;
  h["hit_tracks"] = hardware.hitTracks;

  JsonArray n = out["nodes"].to<JsonArray>();
  for (const NodeTuning& t : nodes) {
    JsonObject o = n.add<JsonObject>();
    JsonArray thr = o["threshold"].to<JsonArray>();
    for (uint16_t v : t.threshold) thr.add(v);
    o["zones_enabled"] = t.zonesEnabled;
  }
}

bool HubSettings::applyGame(JsonObjectConst in, String& error) {
  game::GameSettings next = game;
  if (!readArray(in["zone_points"].as<JsonArrayConst>(), next.zonePoints, 0, 100)) {
    error = "คะแนนโซนต้องเป็นตัวเลข 0–100 จำนวน 5 ค่า";
    return false;
  }
  if (!inRange(in["wrong_penalty"], 0, 100)) {
    error = "คะแนนที่หักต้องอยู่ระหว่าง 0–100";
    return false;
  }
  next.wrongPenalty = in["wrong_penalty"].as<uint8_t>();
  if (!in["grace_ms"].isNull()) {
    if (!inRange(in["grace_ms"], 0, 1000)) {
      error = "ช่วงผ่อนผันต้องอยู่ระหว่าง 0–1000 ms";
      return false;
    }
    next.graceMs = in["grace_ms"].as<uint32_t>();
  }
  JsonArrayConst diff = in["difficulty"].as<JsonArrayConst>();
  if (diff.size() != game::kDifficultyCount) {
    error = "ต้องมีค่าความยาก 3 ระดับ";
    return false;
  }
  for (size_t i = 0; i < game::kDifficultyCount; ++i) {
    JsonObjectConst d = diff[i].as<JsonObjectConst>();
    if (!inRange(d["active_ms"], 300, 20000)) {
      error = "เวลาไฟติดต้องอยู่ระหว่าง 300–20000 ms";
      return false;
    }
    if (!inRange(d["gap_min_ms"], game::kMinGapMs, 10000) || !inRange(d["gap_max_ms"], game::kMinGapMs, 10000)) {
      error = "เวลาพักต้องไม่ต่ำกว่า 1000 ms (1 วินาที)";
      return false;
    }
    next.difficulty[i] = {d["active_ms"].as<uint32_t>(), d["gap_min_ms"].as<uint32_t>(), d["gap_max_ms"].as<uint32_t>()};
  }
  if (!next.isValid()) {
    error = "เวลาพักต่ำสุดต้องไม่มากกว่าเวลาพักสูงสุด";
    return false;
  }
  game = next;
  return true;
}

bool HubSettings::applyHardware(JsonObjectConst in, String& error) {
  HardwareSettings next = hardware;
  if (!inRange(in["lockout_ms"], 20, 1000) || !inRange(in["arbiter_ms"], 2, 30) || !inRange(in["brightness"], 10, 255)) {
    error = "ค่า lockout / arbiter / ความสว่าง อยู่นอกช่วงที่รับได้";
    return false;
  }
  if (!parseColorOrder(in["color_order"].as<const char*>(), next.colorOrder)) {
    error = "ลำดับสีไม่ถูกต้อง";
    return false;
  }
  const long khz = in["led_khz"].as<long>();
  if (khz != 400 && khz != 800) {
    error = "ความเร็ว LED ต้องเป็น 400 หรือ 800 kHz";
    return false;
  }
  next.lockoutMs = in["lockout_ms"].as<uint16_t>();
  next.arbiterWindowMs = in["arbiter_ms"].as<uint8_t>();
  next.brightness = in["brightness"].as<uint8_t>();
  // Sound fields are optional so settings saved by older firmware still load.
  if (!in["volume"].isNull()) {
    if (!inRange(in["volume"], 0, kMaxVolume)) {
      error = "ความดังต้องอยู่ระหว่าง 0–30";
      return false;
    }
    next.volume = in["volume"].as<uint8_t>();
  }
  if (!in["hit_tracks"].isNull()) {
    if (!inRange(in["hit_tracks"], 1, kMaxHitTracks)) {
      error = "จำนวนไฟล์เสียงต้องอยู่ระหว่าง 1–20";
      return false;
    }
    next.hitTracks = in["hit_tracks"].as<uint8_t>();
  }
  next.ledProtocol = khz == 400 ? LedProtocol::Khz400 : LedProtocol::Khz800;
  hardware = next;
  return true;
}

bool HubSettings::applyNodeTuning(uint8_t lane, JsonObjectConst in, String& error) {
  if (lane >= kLaneCount) {
    error = "ไม่มีหุ่นหมายเลขนี้";
    return false;
  }
  NodeTuning next = nodes[lane];
  if (!readArray(in["threshold"].as<JsonArrayConst>(), next.threshold, 10, 4095)) {
    error = "threshold ต้องเป็นตัวเลข 10–4095 จำนวน 5 ค่า";
    return false;
  }
  if (!in["zones_enabled"].isNull()) {
    if (!inRange(in["zones_enabled"], 1, kAllZonesMask)) {
      error = "ต้องเปิดใช้อย่างน้อย 1 โซน";
      return false;
    }
    next.zonesEnabled = in["zones_enabled"].as<uint8_t>();
  }
  nodes[lane] = next;
  return true;
}

bool HubSettings::applyWifi(JsonObjectConst in, String& error) {
  const String ssid = in["ssid"].as<String>();
  const String pass = in["password"].as<String>();
  if (ssid.length() < 1 || ssid.length() > 31) {
    error = "ชื่อ WiFi ต้องยาว 1–31 ตัวอักษร";
    return false;
  }
  if (pass.length() < 8 || pass.length() > 63) {
    error = "รหัส WiFi ต้องยาว 8–63 ตัวอักษร";
    return false;
  }
  if (!inRange(in["channel"], 1, 13)) {
    error = "ช่อง WiFi ต้องอยู่ระหว่าง 1–13";
    return false;
  }
  wifi.ssid = ssid;
  wifi.password = pass;
  wifi.channel = in["channel"].as<uint8_t>();
  return true;
}

bool HubSettings::applyCloud(JsonObjectConst in, String& error) {
  if (in.isNull()) {
    error = "ไม่มีค่า cloud";
    return false;
  }
  CloudSettings next = cloud;
  next.enabled = in["enabled"] | false;
  next.ssid = in["ssid"] | "";
  next.email = in["email"] | "";
  next.ssid.trim();
  next.email.trim();
  const String pass = in["password"] | "";
  const String secret = in["secret"] | "";
  if (pass.length() > 0) next.password = pass;
  if (secret.length() > 0) next.secret = secret;
  if (in["clear_password"] | false) next.password = "";
  if (next.ssid.length() > 32) {
    error = "ชื่อ WiFi ร้านยาวเกิน 32 ตัวอักษร";
    return false;
  }
  if (next.password.length() > 0 && (next.password.length() < 8 || next.password.length() > 63)) {
    error = "รหัส WiFi ร้านต้องยาว 8–63 ตัวอักษร (หรือเว้นว่างถ้าไม่มีรหัส)";
    return false;
  }
  if (next.enabled) {
    if (next.ssid.length() == 0) {
      error = "ใส่ชื่อ WiFi ร้านก่อนเปิดใช้";
      return false;
    }
    if (next.email.indexOf('@') < 1 || next.secret.length() < 6) {
      error = "ใส่อีเมลและรหัสผ่าน (6 ตัวขึ้นไป) ของบัญชี hub ใน Firebase ก่อนเปิดใช้";
      return false;
    }
  }
  if (next.email.length() > 96 || next.secret.length() > 96) {
    error = "อีเมลหรือรหัสผ่านยาวเกินไป";
    return false;
  }
  cloud = next;
  return true;
}

bool HubSettings::applyPin(const String& pin, String& error) {
  if (pin.length() < 4 || pin.length() > 8) {
    error = "PIN ต้องยาว 4–8 ตัว";
    return false;
  }
  for (char c : pin) {
    if (c < '0' || c > '9') {
      error = "PIN ต้องเป็นตัวเลขเท่านั้น";
      return false;
    }
  }
  adminPin = pin;
  return true;
}

bool HubSettings::applyAll(JsonObjectConst root, bool strict, String& error) {
  if (root.isNull() || root["schema"].as<int>() != kSchema) {
    error = "ไฟล์ตั้งค่าไม่ใช่รุ่นที่ระบบนี้อ่านได้";
    return false;
  }
  HubSettings next = *this;
  String e;
  // Each step either succeeds, is skipped (lenient) or aborts with its section name (strict).
  auto step = [&](bool ok, const char* section) {
    if (ok || !strict) return true;
    error = String(section) + ": " + e;
    return false;
  };
  if (!step(next.applyWifi(root["wifi"], e), "WiFi")) return false;
  if (!step(next.applyPin(root["admin_pin"].as<String>(), e), "PIN")) return false;
  if (!step(next.applyGame(root["game"], e), "กติกาเกม")) return false;
  if (!step(next.applyHardware(root["hardware"], e), "ฮาร์ดแวร์")) return false;
  // Optional: backups made before the cloud uplink existed have no "cloud" section.
  if (!root["cloud"].isNull() && !step(next.applyCloud(root["cloud"], e), "Cloud")) return false;
  JsonArrayConst n = root["nodes"].as<JsonArrayConst>();
  e = "ข้อมูลหุ่นไม่ครบ";
  if (!step(n.size() == kLaneCount, "หุ่น")) return false;
  for (uint8_t i = 0; i < kLaneCount && i < n.size(); ++i) {
    if (!step(next.applyNodeTuning(i, n[i], e), "หุ่น")) return false;
  }
  *this = next;
  return true;
}

bool HubSettings::load() {
  File f = storage::data().open(kPath, "r");
  if (!f) return false;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  // Lenient: one bad section doesn't discard the rest.
  String ignored;
  return applyAll(doc.as<JsonObjectConst>(), false, ignored);
}

bool HubSettings::save() const {
  JsonDocument doc;
  toJson(doc.to<JsonObject>(), true);
  // Write to a temp file then rename, so a power cut never leaves a half-written file.
  File f = storage::data().open(kTempPath, "w");
  if (!f) return false;
  const bool ok = serializeJson(doc, f) > 0;
  f.close();
  if (!ok) return false;
  return storage::replaceFile(kTempPath, kPath);
}

}  // namespace tr::hub
