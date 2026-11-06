#pragma once

// Everything the operator can change, persisted as /settings.json on the data partition.

#include <ArduinoJson.h>

#include <array>

#include "game_types.h"
#include "led_types.h"
#include "system_config.h"

namespace tr::hub {

struct NodeTuning {
  std::array<uint16_t, kZoneCount> threshold{{kDefaultThreshold, kDefaultThreshold, kDefaultThreshold,
                                              kDefaultThreshold, kDefaultThreshold}};
  uint8_t zonesEnabled = kAllZonesMask;  // switch off a zone whose light or sensor is broken
};

struct HardwareSettings {
  uint16_t lockoutMs = kDefaultLockoutMs;
  uint8_t arbiterWindowMs = kDefaultArbiterWindowMs;
  uint8_t brightness = kDefaultBrightness;
  ColorOrder colorOrder = ColorOrder::RGB;
  LedProtocol ledProtocol = LedProtocol::Khz800;
  uint8_t volume = kDefaultVolume;  // mannequin speaker, 0..30
  uint8_t hitTracks = 5;            // hit sounds on the mannequin card: sd-card/01/001..005.mp3
};

struct WifiSettings {
  String ssid = "TARGET-RANGE";
  String password = "targetrange";
  uint8_t channel = kDefaultWifiChannel;
};

struct HubSettings {
  WifiSettings wifi;
  String adminPin = "1234";
  game::GameSettings game;
  HardwareSettings hardware;
  std::array<NodeTuning, kLaneCount> nodes;

  bool load();        // false if the file is missing or invalid (defaults stay in place)
  bool save() const;

  // Applies a whole settings object (as written by toJson with secrets).
  // strict: every section must be present and valid, else nothing changes.
  // lenient: bad sections are skipped and keep their current values.
  bool applyAll(JsonObjectConst root, bool strict, String& error);

  void toJson(JsonObject out, bool includeSecrets) const;
  // Each apply* validates first and changes nothing on error; `error` explains why.
  bool applyGame(JsonObjectConst in, String& error);
  bool applyHardware(JsonObjectConst in, String& error);
  bool applyNodeTuning(uint8_t lane, JsonObjectConst in, String& error);
  bool applyWifi(JsonObjectConst in, String& error);
  bool applyPin(const String& pin, String& error);
};

const char* colorOrderName(ColorOrder o);
bool parseColorOrder(const char* s, ColorOrder& out);

}  // namespace tr::hub
