#include "sd_archive.h"

#include <SD.h>

#include "history_store.h"
#include "storage.h"

namespace tr::hub {

namespace {
constexpr uint32_t kSpiHz = 4000000;  // conservative: long jumpers, cheap modules
constexpr uint8_t kQueueDepth = 8;
constexpr uint32_t kTaskStack = 6144;
constexpr UBaseType_t kTaskPriority = 1;  // below the app task (3)
constexpr uint32_t kRetryMs = 5000;       // mount attempts while no card
constexpr uint32_t kProbeMs = 10000;      // health check while mounted
constexpr size_t kTailBytes = 4096;       // enough to hold the last line

uint32_t idOf(const char* line) {
  uint32_t id = 0;
  return HistoryStore::parseLine(String(line), id) ? id : 0;
}
}  // namespace

void SdArchive::begin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t cs) {
  cs_ = cs;
  spi_.begin(sck, miso, mosi, cs);
  queue_ = xQueueCreate(kQueueDepth, sizeof(char*));
  if (mount()) readLastId();
  if (queue_ == nullptr ||
      xTaskCreatePinnedToCore(taskEntry, "sd", kTaskStack, this, kTaskPriority, nullptr, 0) != pdPASS) {
    setError("เริ่มงานบันทึก SD ไม่ได้");
  }
}

void SdArchive::submit(const String& line) {
  if (queue_ == nullptr) return;
  char* copy = strdup(line.c_str());
  if (copy == nullptr) return;
  if (xQueueSend(queue_, &copy, 0) != pdTRUE) free(copy);  // backfill picks it up later
}

SdArchive::Status SdArchive::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Status s = status_;
  s.mounted = mounted_.load();
  s.lastId = lastId_.load();
  return s;
}

fs::FS& SdArchive::fs() { return SD; }

void SdArchive::setError(const String& e) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.error = e;
}

void SdArchive::taskEntry(void* arg) { static_cast<SdArchive*>(arg)->run(); }

void SdArchive::run() {
  // A card that mounted in begin() may have missed games from before it was fitted.
  if (mounted_) backfill();
  uint32_t lastCheckMs = millis();
  for (;;) {
    char* line = nullptr;
    const bool got = xQueueReceive(queue_, &line, pdMS_TO_TICKS(1000)) == pdTRUE;
    const uint32_t nowMs = millis();

    if (!mounted_ && (got || nowMs - lastCheckMs >= kRetryMs)) {
      lastCheckMs = nowMs;
      if (mount()) {
        readLastId();
        backfill();  // covers `line` too: it is already in flash history
      }
    } else if (mounted_ && got) {
      const uint32_t id = idOf(line);
      if (id > lastId_) write(line, id);
      refreshSpace();
    } else if (mounted_ && nowMs - lastCheckMs >= kProbeMs) {
      lastCheckMs = nowMs;
      File dir = SD.open(kDir);
      if (!dir) unmount("ถอดการ์ดออก หรือการ์ดอ่านไม่ได้");
    }
    free(line);
  }
}

bool SdArchive::mount() {
  if (!SD.begin(cs_, spi_, kSpiHz, "/sd", 4, false)) {
    SD.end();
    setError("ไม่พบ SD card");
    return false;
  }
  if (!SD.exists(kDir) && !SD.mkdir(kDir)) {
    SD.end();
    setError("สร้างโฟลเดอร์บนการ์ดไม่ได้ (การ์ดป้องกันการเขียน?)");
    return false;
  }
  mounted_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.error = "";
    status_.totalBytes = SD.totalBytes();
  }
  refreshSpace();
  Serial.printf("# SD archive mounted, %llu MB\n", SD.cardSize() / (1024ull * 1024ull));
  return true;
}

void SdArchive::unmount(const char* why) {
  SD.end();
  mounted_ = false;
  setError(why);
  Serial.printf("# SD archive: %s\n", why);
}

void SdArchive::readLastId() {
  File f = SD.open(kPath, FILE_READ);
  if (!f) return;  // new card: nothing archived yet
  const size_t size = f.size();
  f.seek(size > kTailBytes ? size - kTailBytes : 0);
  // The last complete, valid line wins; a torn final line (power cut) is skipped.
  uint32_t last = 0;
  while (f.available()) {
    const String line = f.readStringUntil('\n');
    uint32_t id = 0;
    if (HistoryStore::parseLine(line, id)) last = id;
  }
  f.close();
  if (last > lastId_) lastId_ = last;
}

void SdArchive::backfill() {
  File f = storage::data().open(HistoryStore::kPath, "r");
  if (!f) return;
  uint32_t added = 0;
  while (f.available() && mounted_) {
    const String line = f.readStringUntil('\n');
    uint32_t id = 0;
    if (!HistoryStore::parseLine(line, id) || id <= lastId_) continue;
    if (!write(line.c_str(), id)) break;
    ++added;
  }
  f.close();
  if (added > 0) Serial.printf("# SD archive: copied %lu games from flash\n", static_cast<unsigned long>(added));
  refreshSpace();
}

bool SdArchive::write(const char* line, uint32_t id) {
  // Open-append-close per game: FAT is not power-safe, so keep the window tiny.
  File f = SD.open(kPath, FILE_APPEND);
  const size_t len = strlen(line);
  const bool ok = f && f.write(reinterpret_cast<const uint8_t*>(line), len) == len && f.write('\n') == 1;
  if (f) f.close();
  if (!ok) {
    unmount("เขียนลงการ์ดไม่สำเร็จ (การ์ดเต็มหรือถูกถอด)");
    return false;
  }
  lastId_ = id;
  std::lock_guard<std::mutex> lock(mutex_);
  ++status_.written;
  return true;
}

void SdArchive::refreshSpace() {
  if (!mounted_) return;
  File f = SD.open(kPath, FILE_READ);
  const uint64_t fileBytes = f ? f.size() : 0;
  if (f) f.close();
  const uint64_t used = SD.usedBytes();
  std::lock_guard<std::mutex> lock(mutex_);
  status_.fileBytes = fileBytes;
  status_.freeBytes = status_.totalBytes > used ? status_.totalBytes - used : 0;
}

}  // namespace tr::hub
