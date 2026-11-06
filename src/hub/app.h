#pragma once

// The hub's brain. A single FreeRTOS task owns all game and node state; the
// radio and web callbacks only push events into its queue, so nothing here
// needs locks. It implements GameListener to turn engine decisions into LED
// commands and phone messages.

#include <Arduino.h>
#include <ArduinoJson.h>

#include <vector>

#include "crosstalk_test.h"
#include "game_engine.h"
#include "history_store.h"
#include "hub_link.h"
#include "hub_settings.h"
#include "node_registry.h"
#include "sd_archive.h"
#include "status_lights.h"
#include "web_server.h"

namespace tr::hub {

class App : public game::GameListener {
 public:
  App();
  bool begin(const HubSettings& settings, bool storageOk);

  // GameListener (called on the app task from inside the engine).
  void setLaneLeds(uint8_t lane, uint8_t zoneMask, const LedSteady& steady, LedColor flash,
                   uint16_t flashMs) override;
  void onGameEvent(const game::GameEvent& event) override;
  void onGameFinished(const game::GameResult& result) override;

 private:
  enum class WebEventKind : uint8_t { Connect, Disconnect, Text, RestoreUploaded };
  struct WebEvent {
    WebEventKind kind;
    uint32_t clientId;
    uint16_t len;
    char text[WebServer::kMaxMessage];
  };

  static void taskEntry(void* arg);
  void run();
  void postWeb(WebEventKind kind, uint32_t clientId, const char* text, size_t len);
  void handleWeb(const WebEvent& e, uint32_t nowMs);
  void tick(uint32_t nowMs);

  // Phone commands.
  void onCommand(uint32_t client, JsonObjectConst cmd, uint32_t nowMs);
  void onAdminCommand(uint32_t client, const char* type, JsonObjectConst cmd, uint32_t nowMs);
  void startGame(uint32_t client, JsonObjectConst cmd, uint32_t nowMs);
  bool isAdmin(uint32_t client) const;
  void reply(uint32_t client, const String& msg) { web_.sendTo(client, msg); }
  void toAdmins(const String& msg);
  bool persist(uint32_t client);
  void publishAdminData();
  void sendStorageStatus();
  void restoreBackup();

  // Node traffic.
  void onHello(uint8_t node, const proto::HelloMsg& m);
  void onHit(uint8_t node, const proto::HitMsg& m, uint32_t nowMs);
  void onHeartbeat(uint8_t node, const proto::HeartbeatMsg& m, int8_t rssi);
  void onPeaks(uint8_t node, const proto::PeaksMsg& m);
  void pushConfig(uint8_t node);
  void pushDesiredLeds(uint8_t node, uint8_t zoneMask);
  void nodeLeds(uint8_t node, uint8_t zoneMask, const LedSteady& steady, LedColor flash = LedColor::Off,
                uint16_t flashMs = 0);
  void setTuning(uint8_t node, bool on);
  void playHitSound(uint8_t node);

  // Crosstalk calibration.
  void startCrosstalk(uint32_t client, uint8_t node, uint8_t shots);
  void stopCrosstalk();
  void lightCrosstalkZone();

  void onNodesDropped(uint8_t dropped, uint32_t nowMs);
  void broadcastState(uint32_t nowMs);
  uint64_t epochNow(uint32_t nowMs) const;
  const char* busyReason() const;

  HubSettings settings_;
  game::GameEngine engine_;
  NodeRegistry registry_;
  HubLink link_;
  WebServer web_;
  HistoryStore history_;
  SdArchive archive_;
  StatusLights lights_;
  CrosstalkTest crosstalk_;
  CrosstalkReport lastReport_;
  bool hasReport_ = false;

  // Radio frames come from the Wi-Fi task, web events from async_tcp; separate
  // queues keep the 1 KB web buffer off the Wi-Fi task's small stack.
  QueueHandle_t radioQueue_ = nullptr;
  QueueHandle_t webQueue_ = nullptr;
  HubLink::RawFrame rxFrame_;  // receive buffers live here, not on the task stack
  WebEvent rxWeb_;
  std::vector<uint32_t> admins_;
  uint8_t tuningMask_ = 0;  // bit n = node n in live tuning mode
  bool storageOk_ = true;
  bool radioOk_ = true;
  uint32_t lastBroadcastRev_ = 0;
  uint32_t lastBroadcastMs_ = 0;
  uint32_t lastNodesMs_ = 0;
  uint32_t lastStorageMs_ = 0;
  uint32_t nodesRev_ = 0;
  uint32_t broadcastNodesRev_ = 0;
  int64_t epochOffsetMs_ = 0;
  bool hasClock_ = false;
  uint32_t rebootAtMs_ = 0;
};

}  // namespace tr::hub
