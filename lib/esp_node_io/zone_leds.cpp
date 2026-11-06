#include "zone_leds.h"

namespace tr::io {

namespace {
// Re-send the frame periodically even if nothing changed, so a pixel that
// glitched (ESD, loose connector re-seated) recovers on its own.
constexpr uint32_t kRefreshMs = 1000;

bool blinkOn(uint32_t nowMs, uint16_t periodMs) {
  const uint16_t period = periodMs < 100 ? 500 : periodMs;
  return (nowMs % period) < period / 2;
}

bool before(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) < 0; }
}  // namespace

bool ZoneLeds::begin(uint8_t pin, uint8_t pixelsPerZone, LedProtocol protocol) {
  pixelsPerZone_ = pixelsPerZone == 0 ? 1 : pixelsPerZone;
  const bool ok = strip_.begin(pin, static_cast<uint8_t>(kZoneCount * pixelsPerZone_), protocol);
  forceRefresh_ = true;
  return ok;
}

void ZoneLeds::configure(uint8_t brightness, ColorOrder order, LedProtocol protocol) {
  brightness_ = brightness;
  strip_.setColorOrder(order);
  strip_.setProtocol(protocol);
  forceRefresh_ = true;
}

void ZoneLeds::apply(uint8_t zoneMask, const LedSteady& steady, LedColor flash, uint16_t flashMs,
                     uint32_t nowMs) {
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    if ((zoneMask & (1u << z)) == 0) continue;
    steady_[z] = steady;
    if (flash != LedColor::Off && flashMs > 0) {
      flashColor_[z] = flash;
      flashUntil_[z] = nowMs + flashMs;
    } else {
      flashColor_[z] = LedColor::Off;
      flashUntil_[z] = nowMs;
    }
  }
}

void ZoneLeds::setOverlay(Overlay overlay, uint32_t untilMs) {
  if (overlay_ == overlay && overlayUntil_ == untilMs) return;
  overlay_ = overlay;
  overlayUntil_ = untilMs;
  forceRefresh_ = true;
}

LedColor ZoneLeds::zoneColor(uint8_t zone, uint32_t nowMs) const {
  switch (overlay_) {
    case Overlay::Boot:
      return LedColor::White;
    case Overlay::Searching:
      return zone == 0 && blinkOn(nowMs, 1000) ? LedColor::Cyan : LedColor::Off;
    case Overlay::LinkLost:
      return zone == 0 && blinkOn(nowMs, 2000) ? LedColor::Red : LedColor::Off;
    case Overlay::Identify:
      return blinkOn(nowMs, 300) ? LedColor::Cyan : LedColor::Off;
    case Overlay::None:
      break;
  }

  if (flashColor_[zone] != LedColor::Off && before(nowMs, flashUntil_[zone])) return flashColor_[zone];

  const LedSteady& s = steady_[zone];
  switch (s.fx) {
    case LedFx::Solid: return s.color;
    case LedFx::Blink: return blinkOn(nowMs, s.periodMs) ? s.color : LedColor::Off;
    case LedFx::Off:
    default: return LedColor::Off;
  }
}

Rgb ZoneLeds::scaled(LedColor c) const {
  const uint8_t idx = static_cast<uint8_t>(c) < static_cast<uint8_t>(LedColor::Count) ? static_cast<uint8_t>(c) : 0;
  const Rgb& p = kPalette[idx];
  return Rgb{static_cast<uint8_t>(p.r * brightness_ / 255), static_cast<uint8_t>(p.g * brightness_ / 255),
             static_cast<uint8_t>(p.b * brightness_ / 255)};
}

void ZoneLeds::render(uint32_t nowMs) {
  if (overlay_ == Overlay::Identify && overlayUntil_ != 0 && !before(nowMs, overlayUntil_)) {
    overlay_ = Overlay::None;
    forceRefresh_ = true;
  }

  std::array<LedColor, kZoneCount> frame{};
  bool changed = forceRefresh_ || (nowMs - lastRefreshMs_) >= kRefreshMs;
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    frame[z] = zoneColor(z, nowMs);
    if (frame[z] != lastFrame_[z]) changed = true;
  }
  if (!changed) return;

  Rgb pixels[PixelStrip::kMaxPixels];
  uint8_t n = 0;
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    const Rgb rgb = scaled(frame[z]);
    for (uint8_t p = 0; p < pixelsPerZone_ && n < PixelStrip::kMaxPixels; ++p) pixels[n++] = rgb;
  }
  strip_.show(pixels);
  lastFrame_ = frame;
  forceRefresh_ = false;
  lastRefreshMs_ = nowMs;
  if (onFrameSent_) onFrameSent_();
}

}  // namespace tr::io
