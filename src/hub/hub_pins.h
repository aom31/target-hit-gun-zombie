#pragma once

#include <cstdint>

#include "sdkconfig.h"

namespace tr::hub::pins {

// Status lights on the hub box front panel, each through 150R to the LED anode.
// Same GPIO numbers on both boards.
constexpr uint8_t kStatusGreen = 16;
constexpr uint8_t kStatusYellow = 17;
constexpr uint8_t kStatusRed = 18;

// Optional microSD module (permanent game archive), SPI.
#if CONFIG_IDF_TARGET_ESP32S3
// ESP32-S3 N16R8 hub: the chip's default SPI pins. The S3 has no GPIO 23,
// GPIO 19/20 are USB and GPIO 35-37 belong to the octal PSRAM.
constexpr uint8_t kSdCs = 10;
constexpr uint8_t kSdMosi = 11;
constexpr uint8_t kSdSck = 12;
constexpr uint8_t kSdMiso = 13;
#else
// Classic ESP32 hub. Avoids GPIO 12 (boot strapping: an SD pull-up there stops
// the ESP32 booting) and the status LEDs.
constexpr uint8_t kSdSck = 14;
constexpr uint8_t kSdMiso = 19;
constexpr uint8_t kSdMosi = 23;
constexpr uint8_t kSdCs = 13;
#endif

}  // namespace tr::hub::pins
