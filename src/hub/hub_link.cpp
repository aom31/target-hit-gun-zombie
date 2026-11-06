#include "hub_link.h"

#include <WiFi.h>
#include <esp_random.h>

#include <cstring>

namespace tr::hub {

using namespace tr::proto;

HubLink* HubLink::instance_ = nullptr;

namespace {
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
}

bool HubLink::begin(uint8_t channel, RawSink sink, Handlers handlers) {
  instance_ = this;
  channel_ = channel;
  sink_ = std::move(sink);
  handlers_ = std::move(handlers);
  seq_ = static_cast<uint16_t>(esp_random());

  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(&HubLink::onReceive);
  addPeer(kBroadcast);
  return true;
}

void HubLink::onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (instance_ == nullptr || !instance_->sink_ || info == nullptr || len <= 0 ||
      len > static_cast<int>(kMaxFrame)) {
    return;
  }
  RawFrame f;
  std::memcpy(f.mac, info->src_addr, 6);
  f.rssi = info->rx_ctrl != nullptr ? static_cast<int8_t>(info->rx_ctrl->rssi) : 0;
  f.len = static_cast<uint8_t>(len);
  std::memcpy(f.data, data, static_cast<size_t>(len));
  instance_->sink_(f);
}

void HubLink::addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peer = {};
  std::memcpy(peer.peer_addr, mac, 6);
  peer.channel = channel_;
  peer.ifidx = WIFI_IF_AP;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

void HubLink::handleFrame(const RawFrame& f, uint32_t nowMs) {
  Header h;
  if (!readHeader(f.data, f.len, h) || !isMannequinNodeId(h.nodeId)) return;
  Peer& peer = peers_[h.nodeId];

  if (h.type == MsgType::Hello) {
    HelloMsg m;
    if (!decode(f.data, f.len, m)) return;
    // A (re)booted node: remember where it is and start its streams afresh.
    if (!peer.known || std::memcmp(peer.mac, f.mac, 6) != 0) {
      if (peer.known) esp_now_del_peer(peer.mac);
      std::memcpy(peer.mac, f.mac, 6);
      addPeer(f.mac);
      peer.known = true;
    }
    peer.inbound.reset();
    forget(h.nodeId);
    sendAck(h.nodeId, h.type, h.seq);
    peer.inbound.accept(h.seq);
    if (handlers_.onHello) handlers_.onHello(h.nodeId, m);
    return;
  }

  if (!peer.known || std::memcmp(peer.mac, f.mac, 6) != 0) return;  // unknown sender: wait for its Hello

  switch (h.type) {
    case MsgType::Ack: {
      AckMsg ack;
      if (!decode(f.data, f.len, ack)) return;
      for (Pending& p : outbox_) {
        if (p.active && p.node == h.nodeId && p.seq == ack.ackSeq && p.type == ack.ackType) p.active = false;
      }
      break;
    }
    case MsgType::Hit: {
      HitMsg m;
      if (!decode(f.data, f.len, m)) return;
      sendAck(h.nodeId, h.type, h.seq);
      if (peer.inbound.accept(h.seq) && handlers_.onHit) handlers_.onHit(h.nodeId, m, nowMs);
      break;
    }
    case MsgType::Heartbeat: {
      HeartbeatMsg m;
      if (decode(f.data, f.len, m) && handlers_.onHeartbeat) handlers_.onHeartbeat(h.nodeId, m, f.rssi);
      break;
    }
    case MsgType::Peaks: {
      PeaksMsg m;
      if (decode(f.data, f.len, m) && handlers_.onPeaks) handlers_.onPeaks(h.nodeId, m);
      break;
    }
    default:
      break;
  }
}

void HubLink::poll(uint32_t nowMs, uint8_t gameState) {
  if (nowMs - lastBeaconMs_ >= kBeaconIntervalMs) {
    lastBeaconMs_ = nowMs;
    BeaconMsg b;
    fillHeader(b.h, MsgType::Beacon, kHubNodeId, nextSeq());
    b.channel = channel_;
    b.gameState = gameState;
    b.hubTimeMs = nowMs;
    esp_now_send(kBroadcast, reinterpret_cast<const uint8_t*>(&b), sizeof(b));
  }

  // Per node, only the oldest queued command is on the air.
  for (uint8_t node = 1; node <= kLaneCount; ++node) {
    Pending* head = nullptr;
    for (Pending& p : outbox_) {
      if (p.active && p.node == node && (head == nullptr || p.order < head->order)) head = &p;
    }
    if (head == nullptr || static_cast<int32_t>(nowMs - head->nextAtMs) < 0) continue;
    if (head->attempts >= kMaxAttempts) {
      head->active = false;  // the heartbeat reconcile in App repairs lost LED state
      continue;
    }
    esp_now_send(peers_[node].mac, head->data, head->len);
    ++head->attempts;
    head->nextAtMs = nowMs + kRetryIntervalMs;
  }
}

bool HubLink::enqueue(uint8_t node, const void* msg, uint8_t len, MsgType type, uint16_t seq) {
  if (!isMannequinNodeId(node) || !peers_[node].known) return false;
  Pending* slot = nullptr;
  for (Pending& p : outbox_) {
    if (!p.active) {
      slot = &p;
      break;
    }
  }
  if (slot == nullptr) {
    // Full: drop this node's oldest command rather than block new ones.
    for (Pending& p : outbox_) {
      if (p.node == node && (slot == nullptr || p.order < slot->order)) slot = &p;
    }
    if (slot == nullptr) return false;
  }
  slot->active = true;
  slot->node = node;
  slot->seq = seq;
  slot->type = type;
  slot->len = len;
  slot->attempts = 0;
  slot->order = orderCounter_++;
  slot->nextAtMs = 0;
  std::memcpy(slot->data, msg, len);
  return true;
}

void HubLink::forget(uint8_t node) {
  for (Pending& p : outbox_) {
    if (p.node == node) p.active = false;
  }
}

bool HubLink::sendLed(uint8_t node, uint8_t zoneMask, const LedSteady& steady, LedColor flash, uint16_t flashMs) {
  LedMsg m;
  fillHeader(m.h, MsgType::Led, kHubNodeId, nextSeq());
  m.zoneMask = zoneMask;
  m.color = steady.color;
  m.fx = steady.fx;
  m.periodMs = steady.periodMs;
  m.flashColor = flash;
  m.flashMs = flashMs;
  return enqueue(node, &m, sizeof(m), MsgType::Led, m.h.seq);
}

bool HubLink::sendConfig(uint8_t node, const ConfigMsg& body) {
  ConfigMsg m = body;
  fillHeader(m.h, MsgType::Config, kHubNodeId, nextSeq());
  return enqueue(node, &m, sizeof(m), MsgType::Config, m.h.seq);
}

bool HubLink::sendCommand(uint8_t node, CommandOp op) {
  CommandMsg m;
  fillHeader(m.h, MsgType::Command, kHubNodeId, nextSeq());
  m.op = op;
  return enqueue(node, &m, sizeof(m), MsgType::Command, m.h.seq);
}

bool HubLink::sendSound(uint8_t node, uint8_t folder, uint8_t track) {
  SoundMsg m;
  fillHeader(m.h, MsgType::Sound, kHubNodeId, nextSeq());
  m.folder = folder;
  m.track = track;
  return enqueue(node, &m, sizeof(m), MsgType::Sound, m.h.seq);
}

void HubLink::sendAck(uint8_t node, MsgType type, uint16_t seq) {
  AckMsg ack;
  fillHeader(ack.h, MsgType::Ack, kHubNodeId, nextSeq());
  ack.ackType = type;
  ack.ackSeq = seq;
  esp_now_send(peers_[node].mac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
}

}  // namespace tr::hub
