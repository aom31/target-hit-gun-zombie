#pragma once

// Per-node tuning persisted in NVS. The hub is the source of truth and pushes
// a Config message whenever the node joins; NVS only bridges reboots.

#include <array>
#include <cstdint>

#include "hit_arbiter.h"
#include "led_types.h"
#include "protocol.h"

namespace tr::node {

struct NodeSettings {
  std::array<uint16_t, kZoneCount> threshold{{kDefaultThreshold, kDefaultThreshold, kDefaultThreshold,
                                              kDefaultThreshold, kDefaultThreshold}};
  uint16_t lockoutMs = kDefaultLockoutMs;
  uint8_t arbiterWindowMs = kDefaultArbiterWindowMs;
  uint8_t brightness = kDefaultBrightness;
  ColorOrder colorOrder = ColorOrder::RGB;
  LedProtocol ledProtocol = LedProtocol::Khz800;
  uint8_t volume = kDefaultVolume;

  void load();
  void save() const;
  // Returns false (and leaves settings unchanged) if the message carries nonsense.
  bool applyFrom(const proto::ConfigMsg& msg);
  sensing::ArbiterConfig arbiterConfig() const;
};

}  // namespace tr::node
