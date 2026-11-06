#pragma once

// System-wide constants shared by the hub, the nodes and the unit tests.
// Nothing in lib/shared may depend on Arduino so it also builds for env:native.

#include <cstdint>

namespace tr {

constexpr uint8_t kZoneCount = 5;
constexpr uint8_t kLaneCount = 2;  // one lane per mannequin
constexpr uint8_t kAllZonesMask = (1u << kZoneCount) - 1;

// Change kSystemId on BOTH hub and nodes if two ranges operate side by side;
// packets with a different id are dropped.
constexpr uint8_t kSystemId = 0x01;
constexpr uint8_t kDefaultWifiChannel = 1;

constexpr uint8_t kFirmwareMajor = 1;
constexpr uint8_t kFirmwareMinor = 0;
constexpr uint8_t kFirmwarePatch = 0;
constexpr const char* kFirmwareVersion = "1.0.0";

// Node ids on the radio link: 0 is the hub, mannequins are 1..kLaneCount.
constexpr uint8_t kHubNodeId = 0;
constexpr uint8_t laneToNodeId(uint8_t lane) { return static_cast<uint8_t>(lane + 1); }
constexpr uint8_t nodeIdToLane(uint8_t nodeId) { return static_cast<uint8_t>(nodeId - 1); }
constexpr bool isMannequinNodeId(uint8_t nodeId) { return nodeId >= 1 && nodeId <= kLaneCount; }

// Default piezo / arbiter tuning; the hub stores the real values per node.
constexpr uint16_t kDefaultThreshold = 300;
constexpr uint16_t kDefaultLockoutMs = 60;
constexpr uint8_t kDefaultArbiterWindowMs = 8;
constexpr uint8_t kDefaultBrightness = 255;

// Mannequin sound (DFPlayer Mini, files on its microSD card).
constexpr uint8_t kDefaultVolume = 25;  // 0..30
constexpr uint8_t kMaxVolume = 30;
constexpr uint8_t kHitSoundFolder = 1;  // /01/001.mp3, /01/002.mp3 ... = scored-hit sounds
constexpr uint8_t kMaxHitTracks = 20;

}  // namespace tr
