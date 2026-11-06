#include "node_settings.h"

#include <Preferences.h>

namespace tr::node {

namespace {
constexpr const char* kNamespace = "trnode";
constexpr uint8_t kSchema = 2;

bool sane(const proto::ConfigMsg& m) {
  for (uint16_t t : m.threshold) {
    if (t < 10 || t > 4095) return false;
  }
  return m.lockoutMs >= 20 && m.lockoutMs <= 1000 && m.arbiterWindowMs >= 2 && m.arbiterWindowMs <= 30 &&
         static_cast<uint8_t>(m.colorOrder) < static_cast<uint8_t>(ColorOrder::Count) &&
         static_cast<uint8_t>(m.ledProtocol) <= static_cast<uint8_t>(LedProtocol::Khz400) && m.volume <= kMaxVolume;
}
}  // namespace

void NodeSettings::load() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;  // first boot: keep defaults
  if (prefs.getUChar("schema", 0) == kSchema) {
    prefs.getBytes("thr", threshold.data(), sizeof(threshold));
    lockoutMs = prefs.getUShort("lock", lockoutMs);
    arbiterWindowMs = prefs.getUChar("win", arbiterWindowMs);
    brightness = prefs.getUChar("bri", brightness);
    colorOrder = static_cast<ColorOrder>(prefs.getUChar("ord", static_cast<uint8_t>(colorOrder)));
    ledProtocol = static_cast<LedProtocol>(prefs.getUChar("proto", static_cast<uint8_t>(ledProtocol)));
    volume = prefs.getUChar("vol", volume);
  }
  prefs.end();
}

void NodeSettings::save() const {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putUChar("schema", kSchema);
  prefs.putBytes("thr", threshold.data(), sizeof(threshold));
  prefs.putUShort("lock", lockoutMs);
  prefs.putUChar("win", arbiterWindowMs);
  prefs.putUChar("bri", brightness);
  prefs.putUChar("ord", static_cast<uint8_t>(colorOrder));
  prefs.putUChar("proto", static_cast<uint8_t>(ledProtocol));
  prefs.putUChar("vol", volume);
  prefs.end();
}

bool NodeSettings::applyFrom(const proto::ConfigMsg& msg) {
  if (!sane(msg)) return false;
  for (uint8_t z = 0; z < kZoneCount; ++z) threshold[z] = msg.threshold[z];
  lockoutMs = msg.lockoutMs;
  arbiterWindowMs = msg.arbiterWindowMs;
  brightness = msg.brightness;
  colorOrder = msg.colorOrder;
  ledProtocol = msg.ledProtocol;
  volume = msg.volume;
  return true;
}

sensing::ArbiterConfig NodeSettings::arbiterConfig() const {
  sensing::ArbiterConfig cfg;
  cfg.threshold = threshold;
  cfg.lockoutMs = lockoutMs;
  cfg.windowMs = arbiterWindowMs;
  return cfg;
}

}  // namespace tr::node
