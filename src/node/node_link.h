#pragma once

// Node side of the ESP-NOW link.
//
// Finds the hub by listening for its beacon (hopping channels if needed),
// registers with a Hello, sends hits reliably (ack + retry) and heartbeats
// periodically, and hands hub commands to the application.

#include <Arduino.h>
#include <esp_now.h>

#include <functional>

#include "hit_arbiter.h"
#include "protocol.h"

namespace tr::node {

class NodeLink {
 public:
  struct Handlers {
    std::function<void(const proto::LedMsg&)> onLed;
    std::function<void(const proto::ConfigMsg&)> onConfig;
    std::function<void(proto::CommandOp)> onCommand;
    std::function<void(const proto::SoundMsg&)> onSound;
    std::function<void(proto::HeartbeatMsg&)> fillHeartbeat;
  };

  bool begin(uint8_t nodeId, Handlers handlers);
  void poll(uint32_t nowMs);

  // detectedAgoMs: how long ago the arbiter decided the hit.
  void sendHit(const sensing::Detection& d, uint32_t detectedAgoMs, uint32_t nowMs);
  void sendPeaks(const sensing::Detection& d);

  bool everPaired() const { return paired_; }
  bool linkUp(uint32_t nowMs) const { return paired_ && (nowMs - lastBeaconMs_) < proto::kLinkTimeoutMs; }
  int8_t rssi() const { return rssi_; }
  uint8_t channel() const { return channel_; }

 private:
  struct RxFrame {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t len;
    uint8_t data[proto::kMaxFrame];
  };

  struct Pending {
    bool active = false;
    uint16_t seq = 0;
    proto::MsgType type = proto::MsgType::Hello;
    uint8_t len = 0;
    uint8_t attempts = 0;
    uint32_t nextAtMs = 0;
    uint32_t detectedAtMs = 0;  // hits only: recompute ageMs on every retry
    uint8_t data[sizeof(proto::HitMsg)] = {};
  };

  static void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len);
  void handleFrame(const RxFrame& f, uint32_t nowMs);
  void pairWith(const uint8_t* mac);
  void setChannel(uint8_t ch);
  void hopChannel(uint32_t nowMs);
  bool queueReliable(const void* msg, uint8_t len, proto::MsgType type, uint16_t seq, uint32_t detectedAtMs,
                     uint32_t nowMs);
  void transmit(Pending& p, uint32_t nowMs);
  void sendAck(proto::MsgType type, uint16_t seq);
  void sendHello(uint32_t nowMs);
  void sendHeartbeat(uint32_t nowMs);
  uint16_t nextSeq() { return seq_++; }

  static NodeLink* instance_;

  Handlers handlers_;
  uint8_t nodeId_ = 1;
  QueueHandle_t rx_ = nullptr;
  uint8_t hubMac_[6] = {};
  bool paired_ = false;
  bool helloAcked_ = false;
  uint8_t channel_ = kDefaultWifiChannel;
  uint32_t lastBeaconMs_ = 0;
  uint32_t lastHopMs_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  uint32_t searchStartedMs_ = 0;
  int8_t rssi_ = 0;
  uint16_t seq_ = 0;
  proto::SeqWindow inbound_;
  Pending outbox_[8];
};

}  // namespace tr::node
