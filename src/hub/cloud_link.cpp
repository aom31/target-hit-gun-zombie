#include "cloud_link.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#include "cloud_config.h"

namespace tr::hub {

namespace {

constexpr uint32_t kTaskStack = 16384;
constexpr UBaseType_t kTaskPriority = 1;  // below the app task (3) and the Wi-Fi stack
constexpr BaseType_t kCore = 0;           // the app task runs on core 1
constexpr uint32_t kHttpTimeoutMs = 8000;
constexpr uint32_t kIdleMs = 150;         // how often to look for new data
constexpr uint32_t kRetryMinMs = 2000;
constexpr uint32_t kRetryMaxMs = 60000;
constexpr uint8_t kResultsPerUpload = 3;
constexpr uint32_t kDeniedRetryMs = 60000;      // 401/403: rules refuse us, a new token won't help
// A TLS session needs ~40 KB of contiguous RAM. Below these the phones' web requests
// would run out of memory and crash the hub, so the cloud waits instead.
constexpr uint32_t kMinFreeHeapForTls = 70000;
constexpr uint32_t kMinBlockForTls = 40000;
constexpr uint32_t kRejoinMinMs = 30000;        // first retry after losing the shop Wi-Fi
constexpr uint32_t kRejoinMaxMs = 5UL * 60000;  // then back off to every 5 minutes

// Reason of the last shop Wi-Fi disconnect (WIFI_REASON_*), set by the Wi-Fi event task.
std::atomic<uint8_t> gDisconnectReason{0};

bool heapOkForTls() {
  return ESP.getFreeHeap() >= kMinFreeHeapForTls && ESP.getMaxAllocHeap() >= kMinBlockForTls;
}

bool wrongPassword(uint8_t reason) {
  return reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
         reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
}

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m_); }

 private:
  SemaphoreHandle_t m_;
};

String gameKey(uint32_t id) {
  char buf[16];
  snprintf(buf, sizeof(buf), "g%06lu", static_cast<unsigned long>(id));
  return buf;
}

// Adds "key": value to a JSON object body being built as text.
void field(String& body, const String& key, const String& rawJson) {
  body += body.length() > 1 ? ",\"" : "\"";
  body += key;
  body += "\":";
  body += rawJson;
}

const char* kServerTime = "{\".sv\":\"timestamp\"}";

}  // namespace

void CloudLink::begin(const CloudSettings& settings) {
  cfg_ = settings;
  enabled_ = settings.enabled && settings.ssid.length() > 0;
  mutex_ = xSemaphoreCreateMutex();
  status_.enabled = enabled_;
  if (enabled_ && !settings.routerFound) {
    // main.cpp did not join (see there). Report why; nothing is queued or uploaded.
    enabled_ = false;
    status_.error = "ไม่พบ WiFi ร้าน \"" + cfg_.ssid +
                    "\" ตอนเปิดเครื่อง (ต้องเป็น 2.4 GHz และอยู่ในระยะ) จึงไม่ต่อเน็ต เพื่อไม่ให้ WiFi สนามหลุด · "
                    "แก้แล้วรีสตาร์ท hub";
    return;
  }
  if (!enabled_ || mutex_ == nullptr) return;
  WiFi.onEvent(
      [](arduino_event_id_t, arduino_event_info_t info) {
        gDisconnectReason.store(info.wifi_sta_disconnected.reason);
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  xTaskCreatePinnedToCore(taskEntry, "cloud", kTaskStack, this, kTaskPriority, nullptr, kCore);
}

void CloudLink::publishHello(const String& json) {
  if (!enabled_) return;
  Lock l(mutex_);
  hello_ = json;
  helloDirty_ = true;
}

void CloudLink::publishState(const String& json) {
  if (!enabled_) return;
  Lock l(mutex_);
  state_ = json;
  stateDirty_ = true;
}

void CloudLink::publishEvent(const String& json) {
  if (!enabled_ || json.length() < 2) return;
  Lock l(mutex_);
  ++eventSeq_;
  // {"t":"event",...} -> {"seq":N,"t":"event",...}
  events_.emplace_back(eventSeq_, String("{\"seq\":") + eventSeq_ + "," + json.substring(1));
  while (events_.size() > kEventSlots) events_.pop_front();  // offline for a while: keep the newest
}

void CloudLink::publishResult(uint32_t id, const String& historyLine, const String& resultMessage) {
  if (!enabled_) return;
  Lock l(mutex_);
  results_.emplace_back(id, historyLine);
  // Every game is also in flash history and on the SD card; the cloud copy is best effort.
  while (results_.size() > kMaxPendingResults) results_.pop_front();
  resultMsg_ = resultMessage;
  resultDirty_ = true;
}

CloudLink::Status CloudLink::status() {
  if (mutex_ == nullptr) return status_;
  Lock l(mutex_);
  Status s = status_;
  s.pendingResults = static_cast<uint16_t>(results_.size());
  s.lastOkAgoS = lastOkMs_ == 0 ? 0 : (millis() - lastOkMs_) / 1000;
  return s;
}

void CloudLink::setError(const String& e) {
  bool changed;
  {
    Lock l(mutex_);
    changed = status_.error != e;
    status_.error = e;
  }
  // Same text as the admin page, so problems can be read over USB (make monitor).
  if (changed && e.length() > 0) {
    Serial.printf("# cloud: %s (heap %lu, block %lu)\n", e.c_str(), (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMaxAllocHeap());
  }
}

void CloudLink::taskEntry(void* arg) { static_cast<CloudLink*>(arg)->run(); }

bool CloudLink::signIn() {
  NetworkClientSecure tls;
  tls.setCACert(cloud::kRootCa);
  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  http.setConnectTimeout(kHttpTimeoutMs);
  const String path = String("/v1/accounts:signInWithPassword?key=") + cloud::kApiKey;
  if (!http.begin(tls, "identitytoolkit.googleapis.com", 443, path, true)) {
    setError("เชื่อมต่อระบบ login ของ Google ไม่ได้");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  JsonDocument req;
  req["email"] = cfg_.email;
  req["password"] = cfg_.secret;
  req["returnSecureToken"] = true;
  String body;
  serializeJson(req, body);
  const int code = http.POST(body);
  const String resp = code > 0 ? http.getString() : String();
  http.end();

  JsonDocument filter;
  filter["idToken"] = true;
  filter["localId"] = true;
  filter["error"]["message"] = true;
  JsonDocument doc;
  deserializeJson(doc, resp, DeserializationOption::Filter(filter));
  if (code == 200 && doc["idToken"].is<const char*>()) {
    idToken_ = doc["idToken"].as<String>();
    tokenAtMs_ = millis();
    // The UID is what /roles/<uid> = "hub" must use in the database (needed to write).
    Serial.printf("# cloud: signed in to Firebase, UID %s (heap %lu, block %lu)\n", doc["localId"] | "?",
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
    Lock l(mutex_);
    status_.authed = true;
    return true;
  }
  idToken_ = "";
  const String why = doc["error"]["message"] | (code > 0 ? String("HTTP ") + code : http.errorToString(code));
  setError(why.indexOf("INVALID") >= 0 || why.indexOf("EMAIL") >= 0 || why.indexOf("PASSWORD") >= 0
               ? "อีเมลหรือรหัสผ่านบัญชี hub ใน Firebase ไม่ถูกต้อง (" + why + ")"
               : "login Firebase ไม่สำเร็จ: " + why);
  Lock l(mutex_);
  status_.authed = false;
  return false;
}

void CloudLink::run() {
  NetworkClientSecure tls;
  tls.setCACert(cloud::kRootCa);
  HTTPClient http;
  http.setReuse(true);  // keep one TLS connection open: a new handshake costs ~1 s and ~40 KB
  http.setTimeout(kHttpTimeoutMs);
  http.setConnectTimeout(kHttpTimeoutMs);

  uint32_t retryMs = kRetryMinMs;
  uint32_t lastSeq = 0;
  uint32_t lastJoinMs = millis();  // main.cpp started the first join at boot
  uint32_t rejoinGapMs = kRejoinMinMs;
  bool joinedOnce = false;

  auto wait = [&](uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); };
  auto backoff = [&]() {
    wait(retryMs);
    retryMs = std::min(retryMs * 2, kRetryMaxMs);
  };

  for (;;) {
    wait(kIdleMs);
    const bool wifi = WiFi.status() == WL_CONNECTED;
    {
      Lock l(mutex_);
      status_.wifi = wifi;
      status_.rssi = wifi ? static_cast<int8_t>(WiFi.RSSI()) : 0;
      status_.ip = wifi ? WiFi.localIP().toString() : String();
      status_.freeHeap = ESP.getFreeHeap();
    }
    if (!wifi) {
      const uint8_t reason = gDisconnectReason.load();
      if (wrongPassword(reason)) {
        // Retrying cannot help; stay off the router until the password is fixed.
        setError("รหัส WiFi ร้าน \"" + cfg_.ssid + "\" ไม่ถูกต้อง · แก้ในหน้านี้แล้วรีสตาร์ท hub");
      } else if (reason == 0) {
        setError("กำลังต่อ WiFi ร้าน \"" + cfg_.ssid + "\"…");  // first attempt still running
      } else if (gameActive_.load() || millis() - lastJoinMs < rejoinGapMs) {
        setError("หลุดจาก WiFi ร้าน \"" + cfg_.ssid + "\" (รหัส " + reason +
                 ") จะลองต่อใหม่เมื่อไม่มีเกม · เกมเล่นได้ปกติ");
      } else {
        gDisconnectReason.store(0);
        WiFi.reconnect();
        lastJoinMs = millis();
        rejoinGapMs = std::min(rejoinGapMs * 2, kRejoinMaxMs);
      }
      wait(1000);
      continue;
    }
    if (rejoinGapMs != kRejoinMinMs || !joinedOnce) {
      Serial.printf("# cloud: joined shop Wi-Fi, IP %s\n", WiFi.localIP().toString().c_str());
      joinedOnce = true;
    }
    rejoinGapMs = kRejoinMinMs;

    if (idToken_.length() == 0 || millis() - tokenAtMs_ > kTokenRefreshMs) {
      // Close the database connection first: two TLS sessions at once would need ~80 KB.
      http.end();
      tls.stop();
      if (!heapOkForTls()) {
        setError("หน่วยความจำ hub เหลือน้อย รอส่งข้อมูลขึ้นเว็บภายหลัง · เกมเล่นได้ปกติ");
        wait(5000);
        continue;
      }
      if (!signIn()) {
        backoff();
        continue;
      }
    }

    // Snapshot what to send; nothing is removed until the upload succeeds.
    String body = "{";
    String sentState, sentHello, sentResultMsg;
    size_t sentEvents = 0, sentResults = 0;
    {
      Lock l(mutex_);
      if (helloDirty_) {
        sentHello = hello_;
        field(body, "live/hello", hello_);
      }
      if (stateDirty_) {
        sentState = state_;
        field(body, "live/state", state_);
        field(body, "live/state_at", kServerTime);
      }
      for (const auto& e : events_) {
        field(body, String("live/events/e") + (e.first % kEventSlots), e.second);
        lastSeq = e.first;
        ++sentEvents;
      }
      for (const auto& r : results_) {
        if (sentResults == kResultsPerUpload) break;
        field(body, "games/" + gameKey(r.first), r.second);
        ++sentResults;
      }
      if (resultDirty_) {
        sentResultMsg = resultMsg_;
        field(body, "live/result", resultMsg_);
      }
    }
    const bool hasData = body.length() > 1;
    if (!hasData && lastOkMs_ != 0 && millis() - lastOkMs_ < kHeartbeatMs) continue;
    field(body, "live/seen", kServerTime);
    field(body, "live/fw", String("\"") + kFirmwareVersion + "\"");
    body += "}";

    if (!tls.connected() && !heapOkForTls()) {
      tls.stop();
      setError("หน่วยความจำ hub เหลือน้อย รอส่งข้อมูลขึ้นเว็บภายหลัง · เกมเล่นได้ปกติ");
      wait(5000);
      continue;
    }

    // One multi-path PATCH at the root: every key is checked against its own rule.
    const String path = "/.json?print=silent&auth=" + idToken_;
    int code = -1;
    if (http.begin(tls, cloud::kDbHost, 443, path, true)) {
      http.addHeader("Content-Type", "application/json");
      code = http.sendRequest("PATCH", body);
      http.end();
    }

    if (code == 200 || code == 204) {
      retryMs = kRetryMinMs;
      lastOkMs_ = millis();
      Lock l(mutex_);
      if (stateDirty_ && state_ == sentState) stateDirty_ = false;
      if (helloDirty_ && hello_ == sentHello) helloDirty_ = false;
      if (resultDirty_ && resultMsg_ == sentResultMsg) resultDirty_ = false;
      // Only this task removes from the front; the app task appends at the back.
      while (sentEvents-- > 0 && !events_.empty() && events_.front().first <= lastSeq) events_.pop_front();
      for (size_t i = 0; i < sentResults && !results_.empty(); ++i) results_.pop_front();
      if (!status_.everOk) Serial.println("# cloud: first upload OK, live view is on");
      status_.everOk = true;
      ++status_.uploads;
      status_.error = "";
      continue;
    }

    if (code == 401 || code == 403) {
      // The rules refuse this account (missing /roles/<uid> = "hub"). Tokens are refreshed
      // before they expire, so signing in again won't help and only burns RAM on TLS:
      // keep the token, close the connection to free its memory, and retry rarely.
      tls.stop();
      setError(String("Firebase ไม่ให้เขียน (HTTP ") + code +
               ") ตรวจว่าใส่ /roles/<UID ของ hub> = \"hub\" และตั้ง rules ตาม firebase/database.rules.json แล้ว"
               " · จะลองใหม่ทุก 1 นาที");
      retryMs = std::max(retryMs, kDeniedRetryMs);
    } else {
      tls.stop();
      setError(code > 0 ? String("ส่งข้อมูลไม่สำเร็จ HTTP ") + code
                        : String("ส่งข้อมูลไม่สำเร็จ: ") + HTTPClient::errorToString(code));
    }
    backoff();
  }
}

}  // namespace tr::hub
