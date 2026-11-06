#include "storage.h"

#include <LittleFS.h>

namespace tr::hub::storage {

namespace {
constexpr const char* kDataLabel = "userdata";
constexpr const char* kWebLabel = "spiffs";
constexpr uint8_t kMaxOpenFiles = 6;

fs::LittleFSFS dataFs;
bool dataOk = false;
}  // namespace

bool begin() {
  dataOk = dataFs.begin(true, "/data", kMaxOpenFiles, kDataLabel);
  // The web partition is never formatted here: an empty one just means
  // `uploadfs` hasn't been run yet, and formatting would hide that.
  LittleFS.begin(false, "/littlefs", kMaxOpenFiles, kWebLabel);
  return dataOk;
}

bool webReady() { return LittleFS.exists("/www/index.html.gz") || LittleFS.exists("/www/index.html"); }

fs::FS& data() { return dataFs; }
fs::FS& web() { return LittleFS; }

Usage dataUsage() {
  if (!dataOk) return {};
  return {dataFs.totalBytes(), dataFs.usedBytes()};
}

bool replaceFile(const char* from, const char* to) {
  if (dataFs.rename(from, to)) return true;
  // Some VFS layers refuse to rename onto an existing file; fall back.
  dataFs.remove(to);
  return dataFs.rename(from, to);
}

}  // namespace tr::hub::storage
