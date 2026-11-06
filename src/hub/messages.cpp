#include "messages.h"

#include "cloud_config.h"

namespace tr::hub::msg {

using namespace tr::game;

namespace {

String serialize(const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  return out;
}

void player(JsonObject o, const PlayerStats& p) {
  o["name"] = p.name;
  o["score"] = p.score;
  o["hits"] = p.hits;
  o["misses"] = p.misses;
  o["wrong"] = p.wrong;
  o["accuracy_pct"] = static_cast<unsigned>(p.accuracy() * 100.0f + 0.5f);
  o["rt_avg_ms"] = p.rtAvgMs();
  o["rt_best_ms"] = p.rtBestMs;
  JsonArray zh = o["zone_hits"].to<JsonArray>();
  JsonArray zp = o["zone_points"].to<JsonArray>();
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    zh.add(p.zoneHits[z]);
    zp.add(p.zonePoints[z]);
  }
}

void gameSettings(JsonObject g, const GameSettings& s) {
  JsonArray pts = g["zone_points"].to<JsonArray>();
  for (uint8_t p : s.zonePoints) pts.add(p);
  g["wrong_penalty"] = s.wrongPenalty;
  g["countdown_ms"] = s.countdownMs;
  JsonArray diff = g["difficulty"].to<JsonArray>();
  for (const auto& d : s.difficulty) {
    JsonObject o = diff.add<JsonObject>();
    o["active_ms"] = d.activeMs;
    o["gap_min_ms"] = d.gapMinMs;
    o["gap_max_ms"] = d.gapMaxMs;
  }
}

template <typename T>
void array5(JsonObject o, const char* key, const T& values) {
  JsonArray a = o[key].to<JsonArray>();
  for (uint8_t z = 0; z < kZoneCount; ++z) a.add(values[z]);
}

}  // namespace

String hello(const HubSettings& s, bool admin) {
  JsonDocument doc;
  doc["t"] = "hello";
  doc["fw"] = kFirmwareVersion;
  doc["admin"] = admin;
  doc["zones"] = kZoneCount;
  doc["lanes"] = kLaneCount;
  // With the cloud uplink on, result-card QR codes point at the public page so
  // players can open their card on mobile data after leaving the hub Wi-Fi.
  if (s.cloud.enabled) doc["public_url"] = cloud::kPublicUrl;
  gameSettings(doc["game"].to<JsonObject>(), s.game);
  return serialize(doc);
}

String publicConfig(const HubSettings& s) {
  JsonDocument doc;
  doc["t"] = "config";
  gameSettings(doc["game"].to<JsonObject>(), s.game);
  return serialize(doc);
}

String state(const GameSnapshot& g, uint32_t nowMs, const NodeRegistry& nodes, const char* busy) {
  JsonDocument doc;
  doc["t"] = "state";
  doc["rev"] = g.revision;
  doc["now"] = nowMs;
  if (busy != nullptr) {
    doc["busy"] = busy;
  } else {
    doc["busy"] = nullptr;
  }

  JsonObject game = doc["game"].to<JsonObject>();
  game["state"] = toString(g.state);
  game["mode"] = static_cast<uint8_t>(g.mode);
  game["difficulty"] = static_cast<uint8_t>(g.difficulty);
  game["duration_s"] = g.durationS;
  game["countdown_ends_at"] = g.countdownEndsAt;
  game["ends_at"] = g.endsAt;
  game["remaining_ms"] = g.remainingMs;
  JsonArray players = game["players"].to<JsonArray>();
  for (uint8_t p = 0; p < g.playerCount; ++p) player(players.add<JsonObject>(), g.players[p]);
  JsonArray lanes = game["lanes"].to<JsonArray>();
  for (const LaneView& l : g.lanes) {
    JsonObject o = lanes.add<JsonObject>();
    o["phase"] = toString(l.phase);
    o["target"] = l.target;
    o["player"] = l.player;
  }

  JsonArray n = doc["nodes"].to<JsonArray>();
  for (uint8_t id = 1; id <= kLaneCount; ++id) {
    const NodeStatus& s = nodes.node(id);
    JsonObject o = n.add<JsonObject>();
    o["id"] = id;
    o["online"] = s.online;
    o["sim"] = s.simulated;
    o["rssi"] = s.rssi;
  }
  return serialize(doc);
}

String event(const GameEvent& e) {
  JsonDocument doc;
  doc["t"] = "event";
  doc["kind"] = toString(e.kind);
  doc["lane"] = e.lane;
  doc["zone"] = e.zone;
  doc["player"] = e.player;
  doc["points"] = e.points;
  doc["rt_ms"] = e.rtMs;
  doc["late"] = e.late;
  return serialize(doc);
}

void result(JsonObject o, const GameResult& r, uint32_t id, uint64_t epochMs) {
  o["id"] = id;
  o["ts"] = epochMs;
  o["mode"] = toString(r.mode);
  o["difficulty"] = toString(r.difficulty);
  o["duration_s"] = r.durationS;
  o["ended"] = toString(r.reason);
  o["winner"] = r.winner;
  JsonArray players = o["players"].to<JsonArray>();
  for (uint8_t p = 0; p < r.playerCount; ++p) player(players.add<JsonObject>(), r.players[p]);
}

String resultMessage(const GameResult& r, uint32_t id, uint64_t epochMs) {
  JsonDocument doc;
  doc["t"] = "result";
  result(doc["game"].to<JsonObject>(), r, id, epochMs);
  return serialize(doc);
}

String nodesDetail(const NodeRegistry& nodes, const HubSettings& s) {
  JsonDocument doc;
  doc["t"] = "nodes";
  JsonArray arr = doc["nodes"].to<JsonArray>();
  for (uint8_t id = 1; id <= kLaneCount; ++id) {
    const NodeStatus& n = nodes.node(id);
    JsonObject o = arr.add<JsonObject>();
    o["id"] = id;
    o["online"] = n.online;
    o["sim"] = n.simulated;
    o["seen"] = n.everSeen;
    o["rssi"] = n.rssi;
    o["uptime_s"] = n.uptimeS;
    o["hits"] = n.hitCount;
    o["fw"] = String(n.fw[0]) + "." + n.fw[1] + "." + n.fw[2];
    o["tuning"] = (n.flags & proto::kFlagTuning) != 0;
    o["calibrated"] = (n.flags & proto::kFlagCalibrated) != 0;
    array5(o, "baseline", n.baseline);
    array5(o, "noise", n.noise);
    array5(o, "threshold", s.nodes[nodeIdToLane(id)].threshold);
    o["zones_enabled"] = s.nodes[nodeIdToLane(id)].zonesEnabled;
  }
  return serialize(doc);
}

String peaks(uint8_t nodeId, uint8_t zone, const uint16_t* values, bool fromHit) {
  JsonDocument doc;
  doc["t"] = "peaks";
  doc["node"] = nodeId;
  doc["zone"] = zone;
  doc["source"] = fromHit ? "hit" : "tune";
  JsonArray a = doc["peaks"].to<JsonArray>();
  for (uint8_t z = 0; z < kZoneCount; ++z) a.add(values[z]);
  return serialize(doc);
}

String crosstalk(const CrosstalkTest& t, const CrosstalkReport* report) {
  JsonDocument doc;
  doc["t"] = "crosstalk";
  doc["node"] = t.nodeId();
  doc["active"] = t.active();
  doc["done"] = t.done();
  doc["zone"] = t.zone();
  doc["shot"] = t.shot();
  doc["shots_per_zone"] = t.shotsPerZone();
  if (report != nullptr) {
    JsonArray m = doc["matrix"].to<JsonArray>();
    for (const auto& row : report->avg) {
      JsonArray r = m.add<JsonArray>();
      for (uint16_t v : row) r.add(v);
    }
    JsonArray sug = doc["suggested"].to<JsonArray>();
    JsonArray ratio = doc["ratio_x10"].to<JsonArray>();
    JsonArray minHit = doc["min_hit"].to<JsonArray>();
    for (uint8_t z = 0; z < kZoneCount; ++z) {
      sug.add(report->suggested[z]);
      ratio.add(report->ratioX10[z]);
      minHit.add(report->minHit[z]);
    }
  }
  return serialize(doc);
}

String settings(const HubSettings& s) {
  JsonDocument doc;
  doc["t"] = "settings";
  JsonObject out = doc["settings"].to<JsonObject>();
  s.toJson(out, false);
  // Only sent to authenticated admins: the join-game poster prints this as a Wi-Fi QR.
  out["wifi"]["password"] = s.wifi.password;
  return serialize(doc);
}

String storageStatus(const tr::hub::storage::Usage& flash, uint16_t games, uint16_t maxGames, const SdArchive::Status& sd) {
  JsonDocument doc;
  doc["t"] = "storage";
  JsonObject f = doc["flash"].to<JsonObject>();
  f["used"] = flash.used;
  f["total"] = flash.total;
  f["games"] = games;
  f["max_games"] = maxGames;
  JsonObject c = doc["sd"].to<JsonObject>();
  c["mounted"] = sd.mounted;
  c["last_id"] = sd.lastId;
  c["written"] = sd.written;
  c["file_bytes"] = sd.fileBytes;
  c["free_bytes"] = sd.freeBytes;
  c["total_bytes"] = sd.totalBytes;
  if (sd.error.length() > 0) c["error"] = sd.error;
  return serialize(doc);
}

String authResult(bool ok) {
  JsonDocument doc;
  doc["t"] = "auth";
  doc["ok"] = ok;
  return serialize(doc);
}

String cloudStatus(const CloudLink::Status& s) {
  JsonDocument doc;
  doc["t"] = "cloud";
  doc["enabled"] = s.enabled;
  doc["wifi"] = s.wifi;
  doc["rssi"] = s.rssi;
  doc["ip"] = s.ip;
  doc["authed"] = s.authed;
  doc["ever_ok"] = s.everOk;
  doc["last_ok_ago_s"] = s.lastOkAgoS;
  doc["uploads"] = s.uploads;
  doc["pending_results"] = s.pendingResults;
  doc["free_heap"] = s.freeHeap;
  doc["error"] = s.error;
  doc["public_url"] = cloud::kPublicUrl;
  return serialize(doc);
}

String error(const char* code, const String& message) {
  JsonDocument doc;
  doc["t"] = "error";
  doc["code"] = code;
  doc["msg"] = message;
  return serialize(doc);
}

String ok(const char* what, const String& message) {
  JsonDocument doc;
  doc["t"] = "ok";
  doc["what"] = what;
  if (message.length() > 0) doc["msg"] = message;
  return serialize(doc);
}

}  // namespace tr::hub::msg
