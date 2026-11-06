#include "node_registry.h"

namespace tr::hub {

void NodeRegistry::onHello(uint8_t nodeId, const proto::HelloMsg& m, uint32_t nowMs) {
  NodeStatus& n = node(nodeId);
  n.fw[0] = m.fwMajor;
  n.fw[1] = m.fwMinor;
  n.fw[2] = m.fwPatch;
  n.resetReason = m.resetReason;
  n.mismatches.fill(0);
  touch(nodeId, nowMs);
}

void NodeRegistry::touch(uint8_t nodeId, uint32_t nowMs) {
  NodeStatus& n = node(nodeId);
  n.lastSeenMs = nowMs;
  n.online = true;
  n.everSeen = true;
}

uint8_t NodeRegistry::onHeartbeat(uint8_t nodeId, const proto::HeartbeatMsg& m, int8_t rssi, uint32_t nowMs) {
  NodeStatus& n = node(nodeId);
  touch(nodeId, nowMs);
  n.rssi = rssi;
  n.uptimeS = m.uptimeS;
  n.flags = m.flags;
  n.hitCount = m.hitCount;
  uint8_t repair = 0;
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    n.baseline[z] = m.baseline[z];
    n.noise[z] = m.noise[z];
    n.threshold[z] = m.threshold[z];
    const LedSteady reported = LedSteady::fromPacked(m.ledState[z]);
    if (reported.sameLook(n.desired[z])) {
      n.mismatches[z] = 0;
    } else if (++n.mismatches[z] >= kMismatchesBeforeRepair) {
      n.mismatches[z] = 0;
      repair |= static_cast<uint8_t>(1u << z);
    }
  }
  return repair;
}

void NodeRegistry::setDesired(uint8_t nodeId, uint8_t zoneMask, const LedSteady& steady) {
  NodeStatus& n = node(nodeId);
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    if (zoneMask & (1u << z)) {
      n.desired[z] = steady;
      n.mismatches[z] = 0;
    }
  }
}

uint8_t NodeRegistry::refresh(uint32_t nowMs) {
  uint8_t dropped = 0;
  for (uint8_t i = 0; i < kLaneCount; ++i) {
    NodeStatus& n = nodes_[i];
    if (n.online && nowMs - n.lastSeenMs > proto::kLinkTimeoutMs) {
      n.online = false;
      dropped |= static_cast<uint8_t>(1u << (i + 1));
    }
  }
  return dropped;
}

bool NodeRegistry::allOnline() const {
  for (const NodeStatus& n : nodes_) {
    if (!n.online) return false;
  }
  return true;
}

}  // namespace tr::hub
