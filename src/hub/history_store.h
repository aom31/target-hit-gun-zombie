#pragma once

// Finished games as JSON lines in /history.jsonl on the data partition,
// newest last, capped at kKeep. The SD card keeps the unlimited archive.

#include <Arduino.h>
#include <FS.h>

namespace tr::hub {

class HistoryStore {
 public:
  static constexpr const char* kPath = "/history.jsonl";
  static constexpr uint16_t kKeep = 300;  // ~200 KB: enough history for long-running leaderboards

  void begin();
  uint32_t nextId() const { return nextId_; }
  uint16_t count() const { return lines_; }
  // Never hand out an id at or below `id` (keeps ids unique against the SD archive).
  void reserveIdsThrough(uint32_t id);

  // line must be a single-line JSON object that already contains its id.
  bool append(const String& line);

  // Replaces the whole history with the remaining lines of `src` (restore).
  // Every line is validated first; on any error nothing changes.
  bool replaceFrom(fs::File& src, uint16_t& games, String& error);

  // A history line is a JSON object with a positive "id" and a "players" array.
  static bool parseLine(const String& line, uint32_t& id);

  // CSV export, one row per player per game.
  static const char* csvHeader();
  static void appendCsvRows(const String& line, String& out);

 private:
  void trim();

  uint32_t nextId_ = 1;
  uint16_t lines_ = 0;
};

}  // namespace tr::hub
