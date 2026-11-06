#pragma once

// ESP-NOW wire protocol between the hub and the mannequin nodes.
//
// Every frame starts with Header. Messages that change state (Hello, Hit, Led,
// Config, Command) are acknowledged and retried by the sender; the receiver
// de-duplicates on (nodeId, seq). Beacon, Heartbeat and Peaks are fire-and-forget.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "led_types.h"
#include "system_config.h"

namespace tr::proto {

constexpr uint16_t kMagic = 0x5447;  // "TG"
constexpr uint8_t kVersion = 2;  // v2: Sound message, volume in Config
constexpr size_t kMaxFrame = 250;  // ESP-NOW payload limit

constexpr uint32_t kBeaconIntervalMs = 500;
constexpr uint32_t kHeartbeatIntervalMs = 1000;
constexpr uint32_t kRetryIntervalMs = 25;
constexpr uint8_t kMaxAttempts = 6;
constexpr uint32_t kLinkTimeoutMs = 3000;

enum class MsgType : uint8_t {
  Beacon = 0x01,
  Hello = 0x02,
  Hit = 0x10,
  Heartbeat = 0x11,
  Peaks = 0x12,
  Led = 0x20,
  Config = 0x21,
  Command = 0x22,
  Sound = 0x23,
  Ack = 0x7F,
};

enum class CommandOp : uint8_t {
  Calibrate = 1,
  TuneOn = 2,
  TuneOff = 3,
  Identify = 4,
  Reboot = 5,
};

// Heartbeat flag bits.
constexpr uint8_t kFlagTuning = 0x01;
constexpr uint8_t kFlagCalibrated = 0x02;
constexpr uint8_t kFlagConfigured = 0x04;

#pragma pack(push, 1)

struct Header {
  uint16_t magic;
  uint8_t version;
  uint8_t systemId;
  MsgType type;
  uint8_t nodeId;
  uint16_t seq;
};

struct BeaconMsg {
  Header h;
  uint8_t channel;
  uint8_t gameState;
  uint32_t hubTimeMs;
};

struct HelloMsg {
  Header h;
  uint8_t fwMajor;
  uint8_t fwMinor;
  uint8_t fwPatch;
  uint8_t resetReason;
  uint8_t zoneCount;
};

struct HitMsg {
  Header h;
  uint8_t zone;
  uint8_t suppressedMask;
  uint16_t ageMs;  // time between detection and this (re)transmission
  uint16_t peaks[kZoneCount];
};

struct HeartbeatMsg {
  Header h;
  uint32_t uptimeS;
  int8_t rssi;
  uint8_t flags;
  uint16_t baseline[kZoneCount];
  uint16_t noise[kZoneCount];
  uint16_t threshold[kZoneCount];
  uint8_t ledState[kZoneCount];  // LedSteady::packed()
  uint32_t hitCount;
};

struct PeaksMsg {
  Header h;
  uint8_t zone;  // arbiter's pick
  uint16_t peaks[kZoneCount];
};

struct LedMsg {
  Header h;
  uint8_t zoneMask;
  LedColor color;
  LedFx fx;
  uint16_t periodMs;
  LedColor flashColor;  // optional overlay shown before the steady state
  uint16_t flashMs;
};

struct ConfigMsg {
  Header h;
  uint16_t threshold[kZoneCount];
  uint16_t lockoutMs;
  uint8_t arbiterWindowMs;
  uint8_t brightness;
  ColorOrder colorOrder;
  LedProtocol ledProtocol;
  uint8_t volume;  // 0..30, DFPlayer scale
};

// Plays /<folder>/<track>.mp3 from the node's microSD card.
struct SoundMsg {
  Header h;
  uint8_t folder;
  uint8_t track;
};

struct CommandMsg {
  Header h;
  CommandOp op;
};

struct AckMsg {
  Header h;
  MsgType ackType;
  uint16_t ackSeq;
};

#pragma pack(pop)

static_assert(sizeof(HeartbeatMsg) <= kMaxFrame, "heartbeat too large");
static_assert(sizeof(HitMsg) <= kMaxFrame, "hit too large");

inline bool requiresAck(MsgType t) {
  switch (t) {
    case MsgType::Hello:
    case MsgType::Hit:
    case MsgType::Led:
    case MsgType::Config:
    case MsgType::Command:
    case MsgType::Sound:
      return true;
    default:
      return false;
  }
}

inline void fillHeader(Header& h, MsgType type, uint8_t nodeId, uint16_t seq) {
  h.magic = kMagic;
  h.version = kVersion;
  h.systemId = kSystemId;
  h.type = type;
  h.nodeId = nodeId;
  h.seq = seq;
}

// Validates the common header; returns false for foreign or malformed frames.
inline bool readHeader(const uint8_t* data, size_t len, Header& out) {
  if (data == nullptr || len < sizeof(Header)) return false;
  std::memcpy(&out, data, sizeof(Header));
  return out.magic == kMagic && out.version == kVersion && out.systemId == kSystemId;
}

template <typename T>
bool decode(const uint8_t* data, size_t len, T& out) {
  if (len < sizeof(T)) return false;
  std::memcpy(&out, data, sizeof(T));
  return true;
}

// Remembers recently seen sequence numbers from one peer to drop retransmitted duplicates.
class SeqWindow {
 public:
  // Returns true if seq is new (and records it).
  bool accept(uint16_t seq) {
    for (uint8_t i = 0; i < kSize; ++i) {
      if (used_[i] && seen_[i] == seq) return false;
    }
    seen_[next_] = seq;
    used_[next_] = true;
    next_ = static_cast<uint8_t>((next_ + 1) % kSize);
    return true;
  }
  void reset() {
    for (bool& u : used_) u = false;
    next_ = 0;
  }

 private:
  static constexpr uint8_t kSize = 24;
  uint16_t seen_[kSize] = {};
  bool used_[kSize] = {};
  uint8_t next_ = 0;
};

}  // namespace tr::proto
