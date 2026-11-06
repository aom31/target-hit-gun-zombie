#pragma once

// HTTP + WebSocket front door for the phones, plus the captive-portal DNS.
//
// Incoming WebSocket traffic is copied into the app's event queue; this class
// never touches game state. Outgoing messages may be sent from the app task.
//
// HTTP endpoints (all files are streamed, never buffered whole in RAM):
//   GET  /history.jsonl          flash history (public: phones build rankings from it)
//   GET  /export.csv             flash history as CSV
//   GET  /api/archive.jsonl|csv  full SD archive
//   GET  /api/backup?pin=        settings + history in one file (contains secrets)
//   POST /api/restore?pin=       upload a backup; the app validates and applies it

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

#include <functional>
#include <mutex>

#include "sd_archive.h"

namespace tr::hub {

class WebServer {
 public:
  using ClientEvent = std::function<void(uint32_t clientId)>;
  using TextEvent = std::function<void(uint32_t clientId, const char* text, size_t len)>;
  using Notify = std::function<void()>;

  static constexpr size_t kMaxMessage = 1024;
  static constexpr const char* kRestorePath = "/restore.tmp";  // on the data partition
  static constexpr size_t kMaxRestoreBytes = 400 * 1024;

  struct Callbacks {
    ClientEvent onConnect;
    ClientEvent onDisconnect;
    TextEvent onText;
    Notify onRestoreUploaded;  // kRestorePath is complete and ready for the app
  };

  void begin(SdArchive& archive, Callbacks callbacks);
  void loop(uint32_t nowMs);  // DNS + housekeeping, call from the app task

  void broadcast(const String& msg);
  void sendTo(uint32_t clientId, const String& msg);
  size_t clientCount() const { return ws_.count(); }

  // Latest state JSON for GET /api/state (debugging).
  void publishState(const String& json);
  // Admin PIN and full settings JSON (with secrets) for the backup endpoint.
  void publishAdmin(const String& pin, const String& settingsJson);

 private:
  void handleWs(AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);
  void addCaptivePortalRoutes();
  void addDataRoutes();
  bool checkPin(AsyncWebServerRequest* req);
  void sendBackup(AsyncWebServerRequest* req);
  void restoreChunk(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total);
  void restoreDone(AsyncWebServerRequest* req);

  AsyncWebServer server_{80};
  AsyncWebSocket ws_{"/ws"};
  DNSServer dns_;
  SdArchive* archive_ = nullptr;
  Callbacks cb_;
  std::mutex stateMutex_;
  String stateJson_ = "{}";
  std::mutex adminMutex_;  // guards the two fields below
  String adminPin_;
  String settingsJson_;
  // PIN guessing: after kMaxBadPins wrong tries, HTTP admin calls are refused for a while.
  uint8_t badPins_ = 0;
  uint32_t lockedUntilMs_ = 0;
  // Restore upload state; only touched from the async_tcp task.
  AsyncWebServerRequest* restoreReq_ = nullptr;
  File restoreFile_;
  bool restoreOk_ = false;
  String restoreError_;
  uint32_t lastCleanupMs_ = 0;
};

}  // namespace tr::hub
