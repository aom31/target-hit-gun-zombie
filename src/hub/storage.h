#pragma once

// The hub's two flash filesystems (see partitions_hub.csv):
//
//   data()  "userdata" partition: settings.json + history.jsonl. Survives
//           `uploadfs`, so updating the web UI never wipes games or settings.
//   web()   "spiffs" partition: the phone UI, rewritten by `uploadfs`.
//
// Both are LittleFS, which is safe against power cuts mid-write.

#include <FS.h>

namespace tr::hub::storage {

struct Usage {
  size_t total = 0;
  size_t used = 0;
};

// Mounts both partitions; formats `userdata` if it has never been used.
// Returns false if the data partition is unusable (history/settings can't be saved).
bool begin();
bool webReady();  // the UI files are present

fs::FS& data();
fs::FS& web();
Usage dataUsage();

// Atomically moves `from` over `to` on the data partition.
bool replaceFile(const char* from, const char* to);

}  // namespace tr::hub::storage
