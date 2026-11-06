#pragma once

// What the hub knows about each mannequin: liveness, last heartbeat, and the
// light state it *should* be showing (used to repair lost LED commands).

#include <array>
#include <cstdint>

#include "led_types.h"
#include "protocol.h"

namespace tr::hub {

struct NodeStatus {
  bool online = false;
  bool everSeen = false;
  bool simulated = false;  // test mode: no board, the hub pretends it is online
  uint32_t lastSeenMs = 0;
  int8_t rssi = 0;
  uint32_t uptimeS = 0;
  uint8_t flags = 0;
  uint32_t hitCount = 0;
  uint8_t fw[3] = {0, 0, 0};
  uint8_t resetReason = 0;
  std::array<uint16_t, kZoneCount> baseline{};
  std::array<uint16_t, kZoneCount> noise{};
  std::array<uint16_t, kZoneCount> threshold{};
  std::array<LedSteady, kZoneCount> desired{};
  std::array<uint8_t, kZoneCount> mismatches{};
};

class NodeRegistry {
 public:
  static constexpr uint8_t kMismatchesBeforeRepair = 2;

  NodeStatus& node(uint8_t nodeId) { return nodes_[index(nodeId)]; }
  const NodeStatus& node(uint8_t nodeId) const { return nodes_[index(nodeId)]; }

  void onHello(uint8_t nodeId, const proto::HelloMsg& m, uint32_t nowMs);
  // Returns a mask of zones whose reported light differs from the desired one for too long.
  uint8_t onHeartbeat(uint8_t nodeId, const proto::HeartbeatMsg& m, int8_t rssi, uint32_t nowMs);
  void touch(uint8_t nodeId, uint32_t nowMs);
  void setDesired(uint8_t nodeId, uint8_t zoneMask, const LedSteady& steady);

  // Updates online flags; returns a bitmask of node ids (bit n = node n) that just went offline.
  uint8_t refresh(uint32_t nowMs);
  bool allOnline() const;

  // Test mode: every node that is not online becomes a simulated one that never
  // times out. A real node that says Hello takes its place. Returns the number simulated.
  uint8_t simulateMissing(uint32_t nowMs);
  // Ends test mode; returns a bitmask of node ids (bit n = node n) that went offline.
  uint8_t clearSimulated();
  bool anySimulated() const;

 private:
  static uint8_t index(uint8_t nodeId) { return nodeId >= 1 && nodeId <= kLaneCount ? nodeId - 1 : 0; }
  std::array<NodeStatus, kLaneCount> nodes_;
};

}  // namespace tr::hub
