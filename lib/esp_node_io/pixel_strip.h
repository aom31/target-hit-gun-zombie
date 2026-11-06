#pragma once

// Minimal WS2811/WS2812 driver on the ESP32 RMT peripheral.
// Small strings only (one module per zone), so one blocking write per frame is fine.

#include <Arduino.h>

#include "led_types.h"

namespace tr::io {

class PixelStrip {
 public:
  static constexpr uint8_t kMaxPixels = 16;

  bool begin(uint8_t pin, uint8_t count, LedProtocol protocol);
  void setColorOrder(ColorOrder order) { order_ = order; }
  void setProtocol(LedProtocol protocol) { protocol_ = protocol; }

  // Sends colours for every pixel. Returns false if the RMT transfer failed.
  bool show(const Rgb* colors);

 private:
  void encodeByte(uint8_t value, rmt_data_t* out) const;

  uint8_t pin_ = 0;
  uint8_t count_ = 0;
  bool ready_ = false;
  LedProtocol protocol_ = LedProtocol::Khz800;
  ColorOrder order_ = ColorOrder::RGB;
  rmt_data_t symbols_[kMaxPixels * 24];
};

}  // namespace tr::io
