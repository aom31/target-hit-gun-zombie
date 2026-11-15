#pragma once

// When a phone last joined the hub Wi-Fi or made an HTTP request (millis).
//
// The cloud upload holds a ~40 KB TLS session; a phone loading the page fires
// ~14 parallel requests that need roughly as much. Both at once run the heap out
// and the web server aborts. CloudLink reads this to step aside (close TLS and
// wait) while phones are loading, so the game page always wins.

#include <Arduino.h>

#include <atomic>

namespace tr::hub {

inline std::atomic<uint32_t> gWebActivityMs{0};

inline void noteWebActivity() { gWebActivityMs.store(millis()); }

inline uint32_t msSinceWebActivity() { return millis() - gWebActivityMs.load(); }

}  // namespace tr::hub
