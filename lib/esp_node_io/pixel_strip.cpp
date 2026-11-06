#include "pixel_strip.h"

namespace tr::io {

namespace {

constexpr uint32_t kRmtTickHz = 10000000;  // 100 ns per tick

struct BitTiming {
  uint16_t zeroHigh, zeroLow, oneHigh, oneLow;  // in 100 ns ticks
};

// 800 kHz: 0 = 0.4/0.85 us, 1 = 0.8/0.45 us. 400 kHz: 0 = 0.5/2.0 us, 1 = 1.2/1.3 us.
constexpr BitTiming kTiming800{4, 9, 8, 5};
constexpr BitTiming kTiming400{5, 20, 12, 13};

void orderBytes(ColorOrder order, const Rgb& c, uint8_t out[3]) {
  switch (order) {
    case ColorOrder::GRB: out[0] = c.g; out[1] = c.r; out[2] = c.b; break;
    case ColorOrder::BRG: out[0] = c.b; out[1] = c.r; out[2] = c.g; break;
    case ColorOrder::RBG: out[0] = c.r; out[1] = c.b; out[2] = c.g; break;
    case ColorOrder::GBR: out[0] = c.g; out[1] = c.b; out[2] = c.r; break;
    case ColorOrder::BGR: out[0] = c.b; out[1] = c.g; out[2] = c.r; break;
    case ColorOrder::RGB:
    default: out[0] = c.r; out[1] = c.g; out[2] = c.b; break;
  }
}

}  // namespace

bool PixelStrip::begin(uint8_t pin, uint8_t count, LedProtocol protocol) {
  pin_ = pin;
  count_ = count > kMaxPixels ? kMaxPixels : count;
  protocol_ = protocol;
  ready_ = rmtInit(pin_, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, kRmtTickHz);
  return ready_;
}

void PixelStrip::encodeByte(uint8_t value, rmt_data_t* out) const {
  const BitTiming& t = protocol_ == LedProtocol::Khz400 ? kTiming400 : kTiming800;
  for (int bit = 7; bit >= 0; --bit) {
    const bool one = (value >> bit) & 1;
    out->level0 = 1;
    out->duration0 = one ? t.oneHigh : t.zeroHigh;
    out->level1 = 0;
    out->duration1 = one ? t.oneLow : t.zeroLow;
    ++out;
  }
}

bool PixelStrip::show(const Rgb* colors) {
  if (!ready_ || count_ == 0) return false;
  rmt_data_t* cursor = symbols_;
  for (uint8_t i = 0; i < count_; ++i) {
    uint8_t bytes[3];
    orderBytes(order_, colors[i], bytes);
    for (uint8_t b : bytes) {
      encodeByte(b, cursor);
      cursor += 8;
    }
  }
  // The line idles low after the transfer, which doubles as the latch/reset pulse.
  return rmtWrite(pin_, symbols_, static_cast<size_t>(count_) * 24, 20);
}

}  // namespace tr::io
