#pragma once

// Mannequin node wiring (see the hardware wiring sheet, section 02).

#include <array>
#include <cstdint>

#include "system_config.h"

namespace tr::pins {

// ADC1 only: ADC2 is unavailable while the radio is on.
constexpr std::array<uint8_t, kZoneCount> kPiezo{{36, 39, 34, 35, 32}};  // Z1..Z5
constexpr uint8_t kPixelData = 25;   // -> 74AHCT125 -> 330R -> first module DIN
constexpr uint8_t kNodeIdJumper = 27;  // open = mannequin 1, jumper to GND = mannequin 2
constexpr uint8_t kOnboardLed = 2;

// DFPlayer Mini on UART2: ESP TX -> 1k -> DFPlayer RX, DFPlayer TX -> ESP RX.
constexpr int8_t kMp3Tx = 17;
constexpr int8_t kMp3Rx = 16;

constexpr uint8_t kPixelsPerZone = 1;  // one WS2811 3-LED module per zone

}  // namespace tr::pins
