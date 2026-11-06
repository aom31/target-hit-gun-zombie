#pragma once

// Optional permanent archive on a microSD card: every finished game is appended
// to /target-range/games.jsonl and never trimmed. Flash keeps the working copy
// (last 300 games); the card keeps everything.
//
// The card is strictly best-effort. All SD access runs on its own low-priority
// task, so a slow, missing or broken card can never stall a game. Games played
// while the card is out are copied over from flash history when it comes back
// (as long as they are still among the last 300).

#include <Arduino.h>
#include <FS.h>
#include <SPI.h>

#include <atomic>
#include <mutex>

namespace tr::hub {

class SdArchive {
 public:
  static constexpr const char* kDir = "/target-range";
  static constexpr const char* kPath = "/target-range/games.jsonl";

  struct Status {
    bool mounted = false;
    uint32_t lastId = 0;      // newest game on the card
    uint32_t written = 0;     // games appended since boot
    uint64_t fileBytes = 0;
    uint64_t freeBytes = 0;
    uint64_t totalBytes = 0;
    String error;             // last problem, empty when healthy
  };

  // Tries to mount once (so lastId() is valid before the first game), then
  // starts the background task. Safe to call without a card fitted.
  void begin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t cs);

  // Queues a finished game (single-line JSON with an "id"). Never blocks.
  void submit(const String& line);

  uint32_t lastId() const { return lastId_.load(); }
  Status status() const;
  fs::FS& fs();

 private:
  static void taskEntry(void* arg);
  void run();
  bool mount();
  void unmount(const char* why);
  void readLastId();
  void backfill();
  bool write(const char* line, uint32_t id);
  void refreshSpace();
  void setError(const String& e);

  SPIClass spi_{VSPI};
  uint8_t cs_ = 0;
  QueueHandle_t queue_ = nullptr;
  std::atomic<uint32_t> lastId_{0};
  std::atomic<bool> mounted_{false};
  mutable std::mutex mutex_;  // guards status_
  Status status_;
};

}  // namespace tr::hub
