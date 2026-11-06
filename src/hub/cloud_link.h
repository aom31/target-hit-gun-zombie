#pragma once

// Mirrors the game to Firebase Realtime Database over the shop Wi-Fi so the
// public web page (GitHub Pages) can show it live (phase 1: view only).
//
// Runs on its own low-priority task on core 0: HTTPS calls take 100-1000 ms
// and must never delay the app task, which handles hits on core 1. The app
// task only hands over JSON strings (latest state wins; events and results
// are queued), so nothing here touches game state.
//
// Database layout (see firebase/database.rules.json):
//   /live/hello      hello message (game settings for the setup page)
//   /live/state      latest state message, /live/state_at = server time it was written
//   /live/events/eN  last 16 events, each with a "seq" number
//   /live/result     latest result message
//   /live/seen       server time of the last upload or heartbeat (hub online?)
//   /games/gNNNNNN   every finished game, same JSON as a history.jsonl line

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <deque>
#include <utility>

#include "hub_settings.h"

namespace tr::hub {

class CloudLink {
 public:
  struct Status {
    bool enabled = false;
    bool wifi = false;       // joined the shop Wi-Fi
    int8_t rssi = 0;
    String ip;
    bool authed = false;     // signed in to Firebase
    uint32_t lastOkAgoS = 0; // seconds since the last successful upload (0 = never)
    bool everOk = false;
    uint32_t uploads = 0;
    uint16_t pendingResults = 0;
    uint32_t freeHeap = 0;
    String error;            // last problem, Thai, for the admin page
  };

  // Starts the upload task when cloud.enabled. Wi-Fi (STA) is started by main.cpp.
  void begin(const CloudSettings& settings);
  bool enabled() const { return enabled_; }

  // Called from the app task. Cheap: copies the string and returns.
  void publishHello(const String& json);
  void publishState(const String& json);
  void publishEvent(const String& json);
  void publishResult(uint32_t id, const String& historyLine, const String& resultMessage);

  Status status();

 private:
  static constexpr size_t kEventSlots = 16;
  static constexpr size_t kMaxPendingResults = 30;
  static constexpr uint32_t kHeartbeatMs = 30000;
  static constexpr uint32_t kTokenRefreshMs = 50UL * 60UL * 1000UL;  // ID tokens last 60 min

  static void taskEntry(void* arg);
  void run();
  bool signIn();
  bool upload(const String& body);
  void setError(const String& e);

  bool enabled_ = false;
  CloudSettings cfg_;
  SemaphoreHandle_t mutex_ = nullptr;

  // Shared with the app task (guarded by mutex_).
  String hello_;
  String state_;
  bool helloDirty_ = false;
  bool stateDirty_ = false;
  std::deque<std::pair<uint32_t, String>> events_;  // (seq, json) not yet uploaded
  uint32_t eventSeq_ = 0;
  std::deque<std::pair<uint32_t, String>> results_; // (game id, history line)
  String resultMsg_;
  bool resultDirty_ = false;
  Status status_;

  // Upload task only.
  String idToken_;
  uint32_t tokenAtMs_ = 0;
  uint32_t lastOkMs_ = 0;
};

}  // namespace tr::hub
