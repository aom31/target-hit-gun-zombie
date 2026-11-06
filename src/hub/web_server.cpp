#include "web_server.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include <memory>

#include "history_store.h"
#include "storage.h"
#include "system_config.h"

namespace tr::hub {

namespace {
constexpr uint16_t kDnsPort = 53;
constexpr uint32_t kCleanupMs = 1000;
constexpr uint8_t kMaxBadPins = 5;
constexpr uint32_t kPinLockoutMs = 60000;
constexpr const char* kBom = "\xEF\xBB\xBF";  // so Excel reads Thai names correctly

// URLs phones probe to detect a captive portal; answering with a redirect makes
// the OS pop up the game page right after joining the Wi-Fi.
constexpr const char* kProbePaths[] = {
    "/generate_204", "/gen_204",       "/hotspot-detect.html", "/library/test/success.html",
    "/connecttest.txt", "/ncsi.txt",   "/success.txt",         "/canonical.html",
    "/redirect",     "/fwlink",
};

String portalUrl() { return String("http://") + WiFi.softAPIP().toString() + "/"; }

// Streams `prefix`, then every line of `file` passed through `transform`, as a
// chunked response. Only one line is held in RAM at a time, so multi-megabyte
// SD archives download fine.
struct LineStream {
  using Transform = void (*)(const String& line, String& out);

  String pending;
  size_t pos = 0;
  File file;
  Transform transform = nullptr;

  size_t fill(uint8_t* buf, size_t maxLen) {
    size_t n = 0;
    while (n < maxLen) {
      if (pos >= pending.length()) {
        pending = "";
        pos = 0;
        if (!file || !file.available()) break;
        const String line = file.readStringUntil('\n');
        if (line.length() < 2) continue;
        if (transform != nullptr) {
          transform(line, pending);
        } else {
          pending = line;
          pending += '\n';
        }
        continue;
      }
      const size_t take = std::min(maxLen - n, pending.length() - pos);
      memcpy(buf + n, pending.c_str() + pos, take);
      n += take;
      pos += take;
    }
    return n;
  }
};

void sendLines(AsyncWebServerRequest* req, const char* contentType, const char* filename, String prefix, File file,
               LineStream::Transform transform) {
  auto stream = std::make_shared<LineStream>();
  stream->pending = std::move(prefix);
  stream->file = file;
  stream->transform = transform;
  AsyncWebServerResponse* res = req->beginChunkedResponse(
      contentType, [stream](uint8_t* buf, size_t maxLen, size_t) -> size_t { return stream->fill(buf, maxLen); });
  res->addHeader("Cache-Control", "no-store");
  if (filename != nullptr) res->addHeader("Content-Disposition", String("attachment; filename=\"") + filename + "\"");
  req->send(res);
}

void sendJsonError(AsyncWebServerRequest* req, int code, const String& msg) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["msg"] = msg;
  String out;
  serializeJson(doc, out);
  req->send(code, "application/json", out);
}
}  // namespace

void WebServer::begin(SdArchive& archive, Callbacks callbacks) {
  archive_ = &archive;
  cb_ = std::move(callbacks);

  ws_.onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data,
                     size_t len) { handleWs(client, type, arg, data, len); });
  server_.addHandler(&ws_);

  server_.on("/api/state", HTTP_GET, [this](AsyncWebServerRequest* req) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    AsyncWebServerResponse* res = req->beginResponse(200, "application/json", stateJson_);
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  addDataRoutes();
  addCaptivePortalRoutes();

  server_.serveStatic("/", storage::web(), "/www/").setDefaultFile("index.html").setCacheControl("no-cache");

  server_.onNotFound([](AsyncWebServerRequest* req) {
    // Anything addressed to another host is a captive-portal probe: send it to the game.
    if (req->host() != WiFi.softAPIP().toString()) {
      req->redirect(portalUrl());
    } else {
      req->send(404, "text/plain", "Not found");
    }
  });

  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dns_.start(kDnsPort, "*", WiFi.softAPIP());
  server_.begin();
}

void WebServer::addDataRoutes() {
  server_.on("/history.jsonl", HTTP_GET, [](AsyncWebServerRequest* req) {
    sendLines(req, "text/plain; charset=utf-8", nullptr, String(), storage::data().open(HistoryStore::kPath, "r"),
              nullptr);
  });

  server_.on("/export.csv", HTTP_GET, [](AsyncWebServerRequest* req) {
    sendLines(req, "text/csv; charset=utf-8", "target-range-history.csv", String(kBom) + HistoryStore::csvHeader(),
              storage::data().open(HistoryStore::kPath, "r"), HistoryStore::appendCsvRows);
  });

  server_.on("/api/archive.jsonl", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!archive_->status().mounted) return sendJsonError(req, 503, "ไม่พบ SD card");
    sendLines(req, "application/x-ndjson", "target-range-archive.jsonl", String(),
              archive_->fs().open(SdArchive::kPath, FILE_READ), nullptr);
  });

  server_.on("/api/archive.csv", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!archive_->status().mounted) return sendJsonError(req, 503, "ไม่พบ SD card");
    sendLines(req, "text/csv; charset=utf-8", "target-range-archive.csv", String(kBom) + HistoryStore::csvHeader(),
              archive_->fs().open(SdArchive::kPath, FILE_READ), HistoryStore::appendCsvRows);
  });

  server_.on("/api/backup", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (checkPin(req)) sendBackup(req);
  });

  server_.on(
      "/api/restore", HTTP_POST, [this](AsyncWebServerRequest* req) { restoreDone(req); }, nullptr,
      [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
        restoreChunk(req, data, len, index, total);
      });
}

bool WebServer::checkPin(AsyncWebServerRequest* req) {
  const uint32_t nowMs = millis();
  if (lockedUntilMs_ != 0 && static_cast<int32_t>(lockedUntilMs_ - nowMs) > 0) {
    sendJsonError(req, 429, "ใส่ PIN ผิดหลายครั้ง รอ 1 นาทีแล้วลองใหม่");
    return false;
  }
  const String pin = req->hasParam("pin") ? req->getParam("pin")->value() : String();
  bool ok;
  {
    std::lock_guard<std::mutex> lock(adminMutex_);
    ok = adminPin_.length() > 0 && pin == adminPin_;
  }
  if (ok) {
    badPins_ = 0;
    return true;
  }
  if (++badPins_ >= kMaxBadPins) {
    badPins_ = 0;
    lockedUntilMs_ = nowMs + kPinLockoutMs;
  }
  sendJsonError(req, 403, "PIN ไม่ถูกต้อง");
  return false;
}

void WebServer::sendBackup(AsyncWebServerRequest* req) {
  // Line 1: header with full settings; then the history lines verbatim.
  String header;
  {
    std::lock_guard<std::mutex> lock(adminMutex_);
    header.reserve(settingsJson_.length() + 96);
    header = "{\"t\":\"target-range-backup\",\"v\":1,\"fw\":\"";
    header += kFirmwareVersion;
    header += "\",\"settings\":";
    header += settingsJson_;
    header += "}\n";
  }
  sendLines(req, "application/x-ndjson", "target-range-backup.jsonl", header,
            storage::data().open(HistoryStore::kPath, "r"), nullptr);
}

void WebServer::restoreChunk(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
  if (index == 0) {
    if (restoreFile_) restoreFile_.close();  // an earlier upload that never finished
    restoreReq_ = req;
    restoreOk_ = false;
    restoreError_ = "";
    if (lockedUntilMs_ != 0 && static_cast<int32_t>(lockedUntilMs_ - millis()) > 0) {
      restoreError_ = "ใส่ PIN ผิดหลายครั้ง รอ 1 นาทีแล้วลองใหม่";
      return;
    }
    if (total == 0 || total > kMaxRestoreBytes) {
      restoreError_ = "ไฟล์ใหญ่เกินไป หรือไม่ใช่ไฟล์สำรองของระบบนี้";
      return;
    }
    const storage::Usage u = storage::dataUsage();
    if (u.total - u.used < total * 2 + 16 * 1024) {  // upload + rewritten history
      restoreError_ = "พื้นที่ในบอร์ดไม่พอสำหรับกู้คืน";
      return;
    }
    restoreFile_ = storage::data().open(kRestorePath, "w");
    if (!restoreFile_) {
      restoreError_ = "เปิดไฟล์ชั่วคราวไม่ได้";
      return;
    }
    restoreOk_ = true;
  }
  if (req != restoreReq_ || !restoreOk_) return;
  if (restoreFile_.write(data, len) != len) {
    restoreOk_ = false;
    restoreError_ = "เขียนไฟล์ชั่วคราวไม่สำเร็จ";
  }
}

void WebServer::restoreDone(AsyncWebServerRequest* req) {
  // PIN is checked last so a wrong PIN still counts towards the lockout.
  if (req != restoreReq_) return sendJsonError(req, 400, "ไม่ได้รับไฟล์");
  restoreReq_ = nullptr;
  if (restoreFile_) restoreFile_.close();
  if (!restoreOk_) {
    storage::data().remove(kRestorePath);
    return sendJsonError(req, 400, restoreError_);
  }
  if (!checkPin(req)) {
    storage::data().remove(kRestorePath);
    return;
  }
  if (cb_.onRestoreUploaded) cb_.onRestoreUploaded();
  req->send(202, "application/json", "{\"ok\":true,\"msg\":\"ได้รับไฟล์แล้ว กำลังตรวจสอบ\"}");
}

void WebServer::addCaptivePortalRoutes() {
  for (const char* path : kProbePaths) {
    server_.on(path, HTTP_ANY, [](AsyncWebServerRequest* req) { req->redirect(portalUrl()); });
  }
}

void WebServer::handleWs(AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      if (cb_.onConnect) cb_.onConnect(client->id());
      break;
    case WS_EVT_DISCONNECT:
      if (cb_.onDisconnect) cb_.onDisconnect(client->id());
      break;
    case WS_EVT_DATA: {
      const AwsFrameInfo* info = static_cast<const AwsFrameInfo*>(arg);
      // Commands are small; fragmented or oversized frames are rejected outright.
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT && len < kMaxMessage) {
        if (cb_.onText) cb_.onText(client->id(), reinterpret_cast<const char*>(data), len);
      } else {
        client->text("{\"t\":\"error\",\"code\":\"too_large\",\"msg\":\"ข้อความยาวเกินไป\"}");
      }
      break;
    }
    default:
      break;
  }
}

void WebServer::loop(uint32_t nowMs) {
  dns_.processNextRequest();
  if (nowMs - lastCleanupMs_ >= kCleanupMs) {
    lastCleanupMs_ = nowMs;
    ws_.cleanupClients();
  }
}

void WebServer::broadcast(const String& msg) {
  if (ws_.count() > 0) ws_.textAll(msg);
}

void WebServer::sendTo(uint32_t clientId, const String& msg) { ws_.text(clientId, msg); }

void WebServer::publishState(const String& json) {
  std::lock_guard<std::mutex> lock(stateMutex_);
  stateJson_ = json;
}

void WebServer::publishAdmin(const String& pin, const String& settingsJson) {
  std::lock_guard<std::mutex> lock(adminMutex_);
  adminPin_ = pin;
  settingsJson_ = settingsJson;
}

}  // namespace tr::hub
