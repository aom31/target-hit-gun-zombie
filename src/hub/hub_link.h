#pragma once

// Hub side of the ESP-NOW link.
//
// Broadcasts beacons, learns node MAC addresses from their Hello, and delivers
// commands reliably. Commands to one node go out strictly in order (one in
// flight at a time) so a retried "light green" can never land after the
// "turn off" that followed it. All methods run on the app task; the radio
// callback only forwards raw frames through the sink.

#include <Arduino.h>
#include <esp_now.h>

#include <functional>

#include "led_types.h"
#include "protocol.h"

namespace tr::hub {

class HubLink {
 public:
  struct RawFrame {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t len;
    uint8_t data[proto::kMaxFrame];
  };
  using RawSink = std::function<void(const RawFrame&)>;

  struct Handlers {
    std::function<void(uint8_t node, const proto::HelloMsg&)> onHello;
    std::function<void(uint8_t node, const proto::HitMsg&, uint32_t nowMs)> onHit;
    std::function<void(uint8_t node, const proto::HeartbeatMsg&, int8_t rssi)> onHeartbeat;
    std::function<void(uint8_t node, const proto::PeaksMsg&)> onPeaks;
  };

  // Wi-Fi must already be running in AP mode on `channel`.
  bool begin(uint8_t channel, RawSink sink, Handlers handlers);
  void handleFrame(const RawFrame& f, uint32_t nowMs);
  void poll(uint32_t nowMs, uint8_t gameState);

  bool sendLed(uint8_t node, uint8_t zoneMask, const LedSteady& steady, LedColor flash, uint16_t flashMs);
  bool sendConfig(uint8_t node, const proto::ConfigMsg& body);
  bool sendCommand(uint8_t node, proto::CommandOp op);
  bool sendSound(uint8_t node, uint8_t folder, uint8_t track);
  void forget(uint8_t node);  // drop queued commands (node went offline)

 private:
  struct Peer {
    bool known = false;
    uint8_t mac[6] = {};
    proto::SeqWindow inbound;
  };
  struct Pending {
    bool active = false;
    uint8_t node = 0;
    uint16_t seq = 0;
    proto::MsgType type = proto::MsgType::Led;
    uint8_t len = 0;
    uint8_t attempts = 0;
    uint32_t order = 0;
    uint32_t nextAtMs = 0;
    uint8_t data[sizeof(proto::ConfigMsg)] = {};
  };

  static void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len);
  bool enqueue(uint8_t node, const void* msg, uint8_t len, proto::MsgType type, uint16_t seq);
  void sendAck(uint8_t node, proto::MsgType type, uint16_t seq);
  void addPeer(const uint8_t* mac);
  uint16_t nextSeq() { return seq_++; }

  static HubLink* instance_;
  static constexpr uint8_t kOutbox = 24;

  RawSink sink_;
  Handlers handlers_;
  uint8_t channel_ = kDefaultWifiChannel;
  Peer peers_[kLaneCount + 1];
  Pending outbox_[kOutbox];
  uint32_t orderCounter_ = 0;
  uint16_t seq_ = 0;
  uint32_t lastBeaconMs_ = 0;
};

}  // namespace tr::hub
