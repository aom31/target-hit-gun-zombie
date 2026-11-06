#pragma once

#include <cstdint>

namespace tr::hub::pins {

// Status lights on the hub box front panel, each through 150R to the LED anode.
constexpr uint8_t kStatusGreen = 16;
constexpr uint8_t kStatusYellow = 17;
constexpr uint8_t kStatusRed = 18;

// Optional microSD module (permanent game archive), SPI. Avoids GPIO 12 (boot
// strapping: an SD pull-up there stops the ESP32 booting) and the status LEDs.
constexpr uint8_t kSdSck = 14;
constexpr uint8_t kSdMiso = 19;
constexpr uint8_t kSdMosi = 23;
constexpr uint8_t kSdCs = 13;

}  // namespace tr::hub::pins
