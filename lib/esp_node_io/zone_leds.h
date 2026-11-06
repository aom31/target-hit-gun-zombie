#pragma once

// Zone light state machine for one mannequin.
//
// Each zone has a steady look (off / solid / blink) plus an optional flash that
// overrides it for a few hundred ms. Overlays for "searching for hub" and
// "identify" sit above everything. render() is called often and only pushes
// pixels to the strip when the computed frame actually changes.

#include <Arduino.h>

#include <array>
#include <functional>

#include "led_types.h"
#include "pixel_strip.h"
#include "system_config.h"

namespace tr::io {

class ZoneLeds {
 public:
  enum class Overlay : uint8_t { None, Searching, LinkLost, Identify, Boot };

  bool begin(uint8_t pin, uint8_t pixelsPerZone, LedProtocol protocol);
  void configure(uint8_t brightness, ColorOrder order, LedProtocol protocol);

  void apply(uint8_t zoneMask, const LedSteady& steady, LedColor flash, uint16_t flashMs, uint32_t nowMs);
  void allOff(uint32_t nowMs) { apply(kAllZonesMask, LedSteady{}, LedColor::Off, 0, nowMs); }
  void setOverlay(Overlay overlay, uint32_t untilMs = 0);
  Overlay overlay() const { return overlay_; }

  // Called after every frame that reaches the strip (used to blank piezo input briefly).
  void onFrameSent(std::function<void()> cb) { onFrameSent_ = std::move(cb); }

  void render(uint32_t nowMs);
  const std::array<LedSteady, kZoneCount>& steady() const { return steady_; }

 private:
  LedColor zoneColor(uint8_t zone, uint32_t nowMs) const;
  Rgb scaled(LedColor c) const;

  PixelStrip strip_;
  uint8_t pixelsPerZone_ = 1;
  uint8_t brightness_ = kDefaultBrightness;
  std::array<LedSteady, kZoneCount> steady_{};
  std::array<LedColor, kZoneCount> flashColor_{};
  std::array<uint32_t, kZoneCount> flashUntil_{};
  Overlay overlay_ = Overlay::None;
  uint32_t overlayUntil_ = 0;
  std::array<LedColor, kZoneCount> lastFrame_{};
  bool forceRefresh_ = true;
  uint32_t lastRefreshMs_ = 0;
  std::function<void()> onFrameSent_;
};

}  // namespace tr::io
