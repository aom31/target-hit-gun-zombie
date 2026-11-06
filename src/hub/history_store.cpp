#include "history_store.h"

#include <ArduinoJson.h>

#include "storage.h"

namespace tr::hub {

namespace {
constexpr const char* kTempPath = "/history.tmp";
constexpr uint16_t kTrimAt = HistoryStore::kKeep + 10;  // trim in batches, not on every append
constexpr size_t kMaxLine = 2048;

void csvField(String& out, const char* s) {
  out += '"';
  for (const char* p = s; *p; ++p) {
    if (*p == '"') out += '"';
    out += *p;
  }
  out += '"';
}

// Swaps the temp file in. LittleFS rename replaces the target atomically, so a
// power cut leaves either the old or the new file, never neither.
bool commitTemp() { return storage::replaceFile(kTempPath, HistoryStore::kPath); }
}  // namespace

bool HistoryStore::parseLine(const String& line, uint32_t& id) {
  if (line.length() < 2 || line.length() > kMaxLine || line[0] != '{') return false;
  JsonDocument filter;
  filter["id"] = true;
  filter["players"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, line, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return false;
  if (!doc["id"].is<uint32_t>() || !doc["players"].is<JsonArrayConst>()) return false;
  id = doc["id"].as<uint32_t>();
  return id > 0;
}

void HistoryStore::begin() {
  lines_ = 0;
  nextId_ = 1;
  File f = storage::data().open(kPath, "r");
  if (!f) return;
  while (f.available()) {
    const String line = f.readStringUntil('\n');
    uint32_t id = 0;
    if (!parseLine(line, id)) continue;
    ++lines_;
    if (id >= nextId_) nextId_ = id + 1;
  }
  f.close();
}

void HistoryStore::reserveIdsThrough(uint32_t id) {
  if (id >= nextId_) nextId_ = id + 1;
}

bool HistoryStore::append(const String& line) {
  File f = storage::data().open(kPath, "a");
  if (!f) return false;
  const bool ok = f.print(line) == line.length() && f.print('\n') == 1;
  f.close();
  if (!ok) return false;
  ++lines_;
  ++nextId_;
  if (lines_ >= kTrimAt) trim();
  return true;
}

void HistoryStore::trim() {
  File in = storage::data().open(kPath, "r");
  if (!in) return;
  File out = storage::data().open(kTempPath, "w");
  if (!out) {
    in.close();
    return;
  }
  uint16_t skip = lines_ > kKeep ? lines_ - kKeep : 0;
  uint16_t kept = 0;
  while (in.available()) {
    const String line = in.readStringUntil('\n');
    if (line.length() < 2) continue;
    if (skip > 0) {
      --skip;
      continue;
    }
    out.print(line);
    out.print('\n');
    ++kept;
  }
  in.close();
  out.close();
  if (commitTemp()) lines_ = kept;
}

bool HistoryStore::replaceFrom(fs::File& src, uint16_t& games, String& error) {
  File out = storage::data().open(kTempPath, "w");
  if (!out) {
    error = "เขียนไฟล์ชั่วคราวไม่ได้ (พื้นที่เต็ม?)";
    return false;
  }
  games = 0;
  uint32_t lineNo = 1;  // line 1 of a backup is its header
  bool ok = true;
  while (src.available()) {
    const String line = src.readStringUntil('\n');
    ++lineNo;
    if (line.length() == 0) continue;
    uint32_t id = 0;
    if (!parseLine(line, id)) {
      error = String("ประวัติบรรทัดที่ ") + lineNo + " เสียหาย";
      ok = false;
      break;
    }
    if (out.print(line) != line.length() || out.print('\n') != 1) {
      error = "พื้นที่เก็บข้อมูลไม่พอ";
      ok = false;
      break;
    }
    ++games;
  }
  out.close();
  if (!ok || !commitTemp()) {
    storage::data().remove(kTempPath);
    if (ok) error = "บันทึกประวัติไม่สำเร็จ";
    return false;
  }
  begin();
  if (lines_ > kKeep) trim();
  return true;
}

const char* HistoryStore::csvHeader() {
  return "id,time_utc_ms,mode,difficulty,duration_s,ended,player,score,hits,misses,wrong,accuracy_pct,"
         "rt_avg_ms,rt_best_ms,winner\r\n";
}

void HistoryStore::appendCsvRows(const String& line, String& out) {
  JsonDocument doc;
  if (line.length() < 2 || deserializeJson(doc, line) != DeserializationError::Ok) return;
  const int winner = doc["winner"] | -1;
  JsonArrayConst players = doc["players"].as<JsonArrayConst>();
  char buf[160];
  for (size_t i = 0; i < players.size(); ++i) {
    JsonObjectConst p = players[i];
    snprintf(buf, sizeof(buf), "%lu,%llu,%s,%s,%u,%s,", static_cast<unsigned long>(doc["id"].as<uint32_t>()),
             static_cast<unsigned long long>(doc["ts"].as<uint64_t>()), doc["mode"] | "", doc["difficulty"] | "",
             doc["duration_s"].as<unsigned>(), doc["ended"] | "");
    out += buf;
    csvField(out, p["name"] | "");
    snprintf(buf, sizeof(buf), ",%ld,%u,%u,%u,%u,%lu,%lu,%s\r\n", p["score"].as<long>(), p["hits"].as<unsigned>(),
             p["misses"].as<unsigned>(), p["wrong"].as<unsigned>(), p["accuracy_pct"].as<unsigned>(),
             static_cast<unsigned long>(p["rt_avg_ms"].as<uint32_t>()),
             static_cast<unsigned long>(p["rt_best_ms"].as<uint32_t>()), winner == static_cast<int>(i) ? "yes" : "");
    out += buf;
  }
}

}  // namespace tr::hub
