#pragma once

// Zone light vocabulary shared by the hub (which decides) and the nodes (which render).
// Colours travel as palette indices so radio packets stay small and both sides agree.

#include <cstdint>

namespace tr {

enum class LedColor : uint8_t { Off = 0, Green, Blue, Red, White, Yellow, Cyan, Magenta, Count };

enum class LedFx : uint8_t { Off = 0, Solid = 1, Blink = 2 };

// Byte order the pixel chip expects on the wire.
enum class ColorOrder : uint8_t { RGB = 0, GRB, BRG, RBG, GBR, BGR, Count };

// WS2811 parts ship in a fast (800 kHz) and a slow (400 kHz) variant.
enum class LedProtocol : uint8_t { Khz800 = 0, Khz400 = 1 };

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

constexpr Rgb kPalette[static_cast<uint8_t>(LedColor::Count)] = {
    {0, 0, 0},        // Off
    {0, 255, 0},      // Green  — shoot here
    {0, 80, 255},     // Blue   — hit confirmed
    {255, 0, 0},      // Red    — wrong zone
    {255, 255, 255},  // White  — countdown / finish
    {255, 160, 0},    // Yellow — paused
    {0, 255, 255},    // Cyan   — identify / tuning target
    {255, 0, 255},    // Magenta
};

// Steady state of one zone, packed into a byte for heartbeats: high nibble fx, low nibble colour.
struct LedSteady {
  LedColor color = LedColor::Off;
  LedFx fx = LedFx::Off;
  uint16_t periodMs = 0;

  uint8_t packed() const {
    return static_cast<uint8_t>((static_cast<uint8_t>(fx) << 4) | static_cast<uint8_t>(color));
  }
  bool sameLook(const LedSteady& o) const {
    if (fx == LedFx::Off || color == LedColor::Off) return o.fx == LedFx::Off || o.color == LedColor::Off;
    return fx == o.fx && color == o.color;
  }
  static LedSteady fromPacked(uint8_t v) {
    LedSteady s;
    s.fx = static_cast<LedFx>(v >> 4);
    s.color = static_cast<LedColor>(v & 0x0F);
    return s;
  }
};

}  // namespace tr
