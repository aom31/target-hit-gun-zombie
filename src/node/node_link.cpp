#include "node_link.h"

#include <WiFi.h>
#include <esp_random.h>
#include <esp_wifi.h>

#include <cstring>

namespace tr::node {

using namespace tr::proto;

NodeLink* NodeLink::instance_ = nullptr;

namespace {
constexpr uint8_t kRxQueueDepth = 16;
constexpr uint32_t kSearchAfterLossMs = 5000;  // stay on the known channel this long before hopping
constexpr uint32_t kHopDwellMs = 650;           // > beacon interval, so one dwell hears a beacon
constexpr uint8_t kMaxChannel = 13;
}  // namespace

bool NodeLink::begin(uint8_t nodeId, Handlers handlers) {
  instance_ = this;
  nodeId_ = nodeId;
  handlers_ = std::move(handlers);
  seq_ = static_cast<uint16_t>(esp_random());

  rx_ = xQueueCreate(kRxQueueDepth, sizeof(RxFrame));
  if (rx_ == nullptr) return false;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);  // lowest latency; the node is mains powered
  setChannel(kDefaultWifiChannel);

  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(&NodeLink::onReceive);
  searchStartedMs_ = millis();
  return true;
}

void NodeLink::onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (instance_ == nullptr || info == nullptr || len <= 0 || len > static_cast<int>(kMaxFrame)) return;
  RxFrame f;
  std::memcpy(f.mac, info->src_addr, 6);
  f.rssi = info->rx_ctrl != nullptr ? static_cast<int8_t>(info->rx_ctrl->rssi) : 0;
  f.len = static_cast<uint8_t>(len);
  std::memcpy(f.data, data, static_cast<size_t>(len));
  xQueueSend(instance_->rx_, &f, 0);
}

void NodeLink::poll(uint32_t nowMs) {
  RxFrame f;
  while (xQueueReceive(rx_, &f, 0) == pdTRUE) handleFrame(f, nowMs);

  for (Pending& p : outbox_) {
    if (!p.active || static_cast<int32_t>(nowMs - p.nextAtMs) < 0) continue;
    if (p.attempts >= kMaxAttempts) {
      p.active = false;  // give up; the heartbeat path will notice a broken link
      continue;
    }
    transmit(p, nowMs);
  }

  if (paired_ && !helloAcked_) {
    bool helloPending = false;
    for (const Pending& p : outbox_) helloPending |= p.active && p.type == MsgType::Hello;
    if (!helloPending) sendHello(nowMs);
  }

  if (paired_ && nowMs - lastHeartbeatMs_ >= kHeartbeatIntervalMs) sendHeartbeat(nowMs);

  hopChannel(nowMs);
}

void NodeLink::hopChannel(uint32_t nowMs) {
  const bool searching = !paired_ ? (nowMs - searchStartedMs_ > kHopDwellMs)
                                  : (nowMs - lastBeaconMs_ > kSearchAfterLossMs);
  if (!searching || nowMs - lastHopMs_ < kHopDwellMs) return;
  lastHopMs_ = nowMs;
  setChannel(static_cast<uint8_t>(channel_ % kMaxChannel + 1));
}

void NodeLink::setChannel(uint8_t ch) {
  channel_ = ch;
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

void NodeLink::handleFrame(const RxFrame& f, uint32_t nowMs) {
  Header h;
  if (!readHeader(f.data, f.len, h) || h.nodeId != kHubNodeId) return;

  if (h.type == MsgType::Beacon) {
    if (!paired_ || std::memcmp(hubMac_, f.mac, 6) != 0) pairWith(f.mac);
    lastBeaconMs_ = nowMs;
    rssi_ = f.rssi;
    return;
  }
  if (!paired_ || std::memcmp(hubMac_, f.mac, 6) != 0) return;  // only obey our hub
  rssi_ = f.rssi;

  if (h.type == MsgType::Ack) {
    AckMsg ack;
    if (!decode(f.data, f.len, ack)) return;
    for (Pending& p : outbox_) {
      if (p.active && p.seq == ack.ackSeq && p.type == ack.ackType) {
        p.active = false;
        if (p.type == MsgType::Hello) helloAcked_ = true;
      }
    }
    return;
  }

  // Everything else from the hub is reliable: always ack, act only once.
  sendAck(h.type, h.seq);
  if (!inbound_.accept(h.seq)) return;

  switch (h.type) {
    case MsgType::Led: {
      LedMsg m;
      if (decode(f.data, f.len, m) && handlers_.onLed) handlers_.onLed(m);
      break;
    }
    case MsgType::Config: {
      ConfigMsg m;
      if (decode(f.data, f.len, m) && handlers_.onConfig) handlers_.onConfig(m);
      break;
    }
    case MsgType::Command: {
      CommandMsg m;
      if (decode(f.data, f.len, m) && handlers_.onCommand) handlers_.onCommand(m.op);
      break;
    }
    case MsgType::Sound: {
      SoundMsg m;
      if (decode(f.data, f.len, m) && handlers_.onSound) handlers_.onSound(m);
      break;
    }
    default:
      break;
  }
}

void NodeLink::pairWith(const uint8_t* mac) {
  if (paired_) esp_now_del_peer(hubMac_);
  std::memcpy(hubMac_, mac, 6);

  esp_now_peer_info_t peer = {};
  std::memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;  // follow the current channel
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  paired_ = true;
  helloAcked_ = false;
  inbound_.reset();
  for (Pending& p : outbox_) p.active = false;
}

bool NodeLink::queueReliable(const void* msg, uint8_t len, MsgType type, uint16_t seq, uint32_t detectedAtMs,
                             uint32_t nowMs) {
  for (Pending& p : outbox_) {
    if (p.active) continue;
    p.active = true;
    p.seq = seq;
    p.type = type;
    p.len = len;
    p.attempts = 0;
    p.nextAtMs = nowMs;
    p.detectedAtMs = detectedAtMs;
    std::memcpy(p.data, msg, len);
    transmit(p, nowMs);
    return true;
  }
  return false;  // outbox full: the hub is unreachable anyway
}

void NodeLink::transmit(Pending& p, uint32_t nowMs) {
  if (p.type == MsgType::Hit) {
    HitMsg* hit = reinterpret_cast<HitMsg*>(p.data);
    const uint32_t age = nowMs - p.detectedAtMs;
    hit->ageMs = static_cast<uint16_t>(age > 0xFFFF ? 0xFFFF : age);
  }
  esp_now_send(hubMac_, p.data, p.len);
  ++p.attempts;
  p.nextAtMs = nowMs + kRetryIntervalMs;
}

void NodeLink::sendAck(MsgType type, uint16_t seq) {
  AckMsg ack;
  fillHeader(ack.h, MsgType::Ack, nodeId_, nextSeq());
  ack.ackType = type;
  ack.ackSeq = seq;
  esp_now_send(hubMac_, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
}

void NodeLink::sendHello(uint32_t nowMs) {
  HelloMsg m;
  fillHeader(m.h, MsgType::Hello, nodeId_, nextSeq());
  m.fwMajor = kFirmwareMajor;
  m.fwMinor = kFirmwareMinor;
  m.fwPatch = kFirmwarePatch;
  m.resetReason = static_cast<uint8_t>(esp_reset_reason());
  m.zoneCount = kZoneCount;
  queueReliable(&m, sizeof(m), MsgType::Hello, m.h.seq, nowMs, nowMs);
}

void NodeLink::sendHeartbeat(uint32_t nowMs) {
  lastHeartbeatMs_ = nowMs;
  HeartbeatMsg m = {};
  fillHeader(m.h, MsgType::Heartbeat, nodeId_, nextSeq());
  m.uptimeS = nowMs / 1000;
  m.rssi = rssi_;
  if (handlers_.fillHeartbeat) handlers_.fillHeartbeat(m);
  esp_now_send(hubMac_, reinterpret_cast<const uint8_t*>(&m), sizeof(m));
}

void NodeLink::sendHit(const sensing::Detection& d, uint32_t detectedAgoMs, uint32_t nowMs) {
  if (!paired_) return;
  HitMsg m;
  fillHeader(m.h, MsgType::Hit, nodeId_, nextSeq());
  m.zone = d.zone;
  m.suppressedMask = d.suppressedMask;
  m.ageMs = 0;
  for (uint8_t z = 0; z < kZoneCount; ++z) m.peaks[z] = d.peaks[z];
  queueReliable(&m, sizeof(m), MsgType::Hit, m.h.seq, nowMs - detectedAgoMs, nowMs);
}

void NodeLink::sendPeaks(const sensing::Detection& d) {
  if (!paired_) return;
  PeaksMsg m;
  fillHeader(m.h, MsgType::Peaks, nodeId_, nextSeq());
  m.zone = d.zone;
  for (uint8_t z = 0; z < kZoneCount; ++z) m.peaks[z] = d.peaks[z];
  esp_now_send(hubMac_, reinterpret_cast<const uint8_t*>(&m), sizeof(m));
}

}  // namespace tr::node
