#include "app.h"

#include <esp_random.h>

#include <algorithm>

#include "hub_pins.h"
#include "messages.h"
#include "storage.h"

namespace tr::hub {

using namespace tr::game;

namespace {

constexpr uint8_t kRadioQueueDepth = 32;
constexpr uint8_t kWebQueueDepth = 12;
constexpr uint32_t kTaskStack = 12288;
constexpr UBaseType_t kTaskPriority = 3;
constexpr uint32_t kStateMinIntervalMs = 50;
constexpr uint32_t kStateKeepaliveMs = 1000;
constexpr uint32_t kNodesIntervalMs = 1000;
constexpr uint32_t kStorageIntervalMs = 5000;
constexpr uint32_t kRestoreRebootMs = 2000;
constexpr const char* kBackupMagic = "target-range-backup";
constexpr size_t kMaxNameChars = 16;
constexpr uint16_t kLedTestMs = 2000;

constexpr LedSteady kOff{};
constexpr LedSteady kCrosstalkTarget{LedColor::Cyan, LedFx::Solid, 0};

// Trims, drops control characters and limits to kMaxNameChars UTF-8 code points.
String cleanName(const char* raw, uint8_t index) {
  String in = raw == nullptr ? String() : String(raw);
  in.trim();
  String out;
  size_t chars = 0;
  for (size_t i = 0; i < in.length() && chars < kMaxNameChars;) {
    const uint8_t c = static_cast<uint8_t>(in[i]);
    const size_t width = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    if (c >= 0x20 && c != 0x7F && i + width <= in.length()) {
      out += in.substring(i, i + width);
      ++chars;
    }
    i += width;
  }
  if (out.length() == 0) out = String("ผู้เล่น ") + (index + 1);
  return out;
}

String startErrorText(StartError e) {
  switch (e) {
    case StartError::Busy: return "มีเกมกำลังเล่นอยู่";
    case StartError::BadMode: return "โหมดเกมไม่ถูกต้อง";
    case StartError::BadDuration: return "เวลาเกมต้องอยู่ระหว่าง 10–600 วินาที";
    case StartError::BadDifficulty: return "ระดับความยากไม่ถูกต้อง";
    case StartError::BadName: return "ชื่อผู้เล่นไม่ถูกต้อง";
    case StartError::NoZones: return "หุ่นต้องเปิดใช้อย่างน้อย 1 โซน";
    case StartError::None: break;
  }
  return "";
}

bool validNode(JsonObjectConst cmd, uint8_t& node) {
  const long n = cmd["node"] | 0L;
  if (!isMannequinNodeId(static_cast<uint8_t>(n)) || n < 1) return false;
  node = static_cast<uint8_t>(n);
  return true;
}

}  // namespace

App::App() : engine_(*this) {}

bool App::begin(const HubSettings& settings, bool storageOk) {
  settings_ = settings;
  storageOk_ = storageOk;
  engine_.setSettings(settings_.game);

  lights_.begin(pins::kStatusGreen, pins::kStatusYellow, pins::kStatusRed);
  history_.begin();
  // Mount the SD archive before the first game so ids continue after the
  // newest archived game, even on a fresh board with an old card.
  archive_.begin(pins::kSdSck, pins::kSdMiso, pins::kSdMosi, pins::kSdCs);
  history_.reserveIdsThrough(archive_.lastId());
  storage::data().remove(WebServer::kRestorePath);  // leftover from an interrupted restore

  radioQueue_ = xQueueCreate(kRadioQueueDepth, sizeof(HubLink::RawFrame));
  webQueue_ = xQueueCreate(kWebQueueDepth, sizeof(WebEvent));
  if (radioQueue_ == nullptr || webQueue_ == nullptr) return false;

  HubLink::Handlers h;
  h.onHello = [this](uint8_t node, const proto::HelloMsg& m) { onHello(node, m); };
  h.onHit = [this](uint8_t node, const proto::HitMsg& m, uint32_t nowMs) { onHit(node, m, nowMs); };
  h.onHeartbeat = [this](uint8_t node, const proto::HeartbeatMsg& m, int8_t rssi) { onHeartbeat(node, m, rssi); };
  h.onPeaks = [this](uint8_t node, const proto::PeaksMsg& m) { onPeaks(node, m); };
  // Both callbacks run on other tasks and must never block them.
  radioOk_ = link_.begin(
      settings_.wifi.channel, [this](const HubLink::RawFrame& f) { xQueueSend(radioQueue_, &f, 0); }, h);

  WebServer::Callbacks cb;
  cb.onConnect = [this](uint32_t id) { postWeb(WebEventKind::Connect, id, nullptr, 0); };
  cb.onDisconnect = [this](uint32_t id) { postWeb(WebEventKind::Disconnect, id, nullptr, 0); };
  cb.onText = [this](uint32_t id, const char* text, size_t len) { postWeb(WebEventKind::Text, id, text, len); };
  cb.onRestoreUploaded = [this]() { postWeb(WebEventKind::RestoreUploaded, 0, nullptr, 0); };
  web_.begin(archive_, std::move(cb));
  publishAdminData();

  return xTaskCreatePinnedToCore(taskEntry, "app", kTaskStack, this, kTaskPriority, nullptr, 1) == pdPASS;
}

void App::postWeb(WebEventKind kind, uint32_t clientId, const char* text, size_t len) {
  static WebEvent scratch;  // only the async_tcp task posts, so one buffer is enough
  scratch.kind = kind;
  scratch.clientId = clientId;
  scratch.len = static_cast<uint16_t>(len);
  if (text != nullptr) memcpy(scratch.text, text, len);
  scratch.text[len] = '\0';
  xQueueSend(webQueue_, &scratch, 0);
}

void App::taskEntry(void* arg) { static_cast<App*>(arg)->run(); }

void App::run() {
  for (;;) {
    // Radio traffic is latency sensitive (hits): wait on it, then sweep web events.
    if (xQueueReceive(radioQueue_, &rxFrame_, pdMS_TO_TICKS(4)) == pdTRUE) {
      link_.handleFrame(rxFrame_, millis());
      for (uint8_t i = 0; i < 8 && xQueueReceive(radioQueue_, &rxFrame_, 0) == pdTRUE; ++i) {
        link_.handleFrame(rxFrame_, millis());
      }
    }
    for (uint8_t i = 0; i < 4 && xQueueReceive(webQueue_, &rxWeb_, 0) == pdTRUE; ++i) handleWeb(rxWeb_, millis());
    tick(millis());
  }
}

void App::handleWeb(const WebEvent& e, uint32_t nowMs) {
  switch (e.kind) {
    case WebEventKind::Connect:
      reply(e.clientId, msg::hello(settings_, false));
      reply(e.clientId, msg::state(engine_.snapshot(nowMs), nowMs, registry_, busyReason()));
      if (engine_.state() == GameState::Finished) {
        reply(e.clientId, msg::resultMessage(engine_.lastResult(), history_.nextId() - 1, 0));
      }
      break;
    case WebEventKind::Disconnect:
      admins_.erase(std::remove(admins_.begin(), admins_.end(), e.clientId), admins_.end());
      break;
    case WebEventKind::RestoreUploaded:
      restoreBackup();
      break;
    case WebEventKind::Text: {
      JsonDocument doc;
      if (deserializeJson(doc, e.text, e.len) != DeserializationError::Ok || !doc["t"].is<const char*>()) {
        reply(e.clientId, msg::error("bad_json", "รูปแบบคำสั่งไม่ถูกต้อง"));
        break;
      }
      onCommand(e.clientId, doc.as<JsonObjectConst>(), nowMs);
      break;
    }
  }
}

void App::tick(uint32_t nowMs) {
  link_.poll(nowMs, static_cast<uint8_t>(engine_.state()));

  const uint8_t dropped = registry_.refresh(nowMs);
  if (dropped != 0) {
    ++nodesRev_;
    for (uint8_t node = 1; node <= kLaneCount; ++node) {
      if ((dropped & (1u << node)) == 0) continue;
      link_.forget(node);
      if (engine_.isActive() && engine_.state() != GameState::Paused && engine_.laneInUse(nodeIdToLane(node))) {
        engine_.pause(nowMs);
        web_.broadcast(msg::error("node_offline", String("หุ่น ") + node + " ขาดการเชื่อมต่อ เกมหยุดชั่วคราว"));
      }
      if (crosstalk_.active() && crosstalk_.nodeId() == node) stopCrosstalk();
    }
  }

  engine_.tick(nowMs);
  web_.loop(nowMs);

  if (!storageOk_ || !radioOk_) {
    lights_.set(StatusLights::Mode::Fault);
  } else {
    lights_.set(registry_.allOnline() ? StatusLights::Mode::Ready : StatusLights::Mode::WaitingForNodes);
  }
  lights_.update(nowMs);

  const bool changed = engine_.revision() != lastBroadcastRev_ || nodesRev_ != broadcastNodesRev_;
  if ((changed && nowMs - lastBroadcastMs_ >= kStateMinIntervalMs) || nowMs - lastBroadcastMs_ >= kStateKeepaliveMs) {
    broadcastState(nowMs);
  }
  if (!admins_.empty() && nowMs - lastNodesMs_ >= kNodesIntervalMs) {
    lastNodesMs_ = nowMs;
    toAdmins(msg::nodesDetail(registry_, settings_));
  }
  if (!admins_.empty() && nowMs - lastStorageMs_ >= kStorageIntervalMs) {
    lastStorageMs_ = nowMs;
    sendStorageStatus();
  }
  // A card fitted after boot may hold newer games than flash: never reuse their ids.
  history_.reserveIdsThrough(archive_.lastId());

  if (rebootAtMs_ != 0 && static_cast<int32_t>(nowMs - rebootAtMs_) >= 0) ESP.restart();
}

void App::broadcastState(uint32_t nowMs) {
  lastBroadcastMs_ = nowMs;
  lastBroadcastRev_ = engine_.revision();
  broadcastNodesRev_ = nodesRev_;
  const String json = msg::state(engine_.snapshot(nowMs), nowMs, registry_, busyReason());
  web_.broadcast(json);
  web_.publishState(json);
}

const char* App::busyReason() const { return crosstalk_.active() ? "tuning" : nullptr; }

uint64_t App::epochNow(uint32_t nowMs) const {
  return hasClock_ ? static_cast<uint64_t>(epochOffsetMs_ + static_cast<int64_t>(nowMs)) : 0;
}

bool App::isAdmin(uint32_t client) const {
  return std::find(admins_.begin(), admins_.end(), client) != admins_.end();
}

void App::toAdmins(const String& m) {
  for (uint32_t id : admins_) web_.sendTo(id, m);
}

bool App::persist(uint32_t client) {
  publishAdminData();
  if (settings_.save()) return true;
  reply(client, msg::error("storage", "บันทึกค่าลงหน่วยความจำไม่สำเร็จ"));
  return false;
}

void App::publishAdminData() {
  JsonDocument doc;
  settings_.toJson(doc.to<JsonObject>(), true);
  String json;
  serializeJson(doc, json);
  web_.publishAdmin(settings_.adminPin, json);
}

void App::sendStorageStatus() {
  toAdmins(msg::storageStatus(storage::dataUsage(), history_.count(), HistoryStore::kKeep, archive_.status()));
}

void App::restoreBackup() {
  // Runs on the app task, which owns settings and history. The upload was
  // written to kRestorePath by the web server and its PIN already checked.
  fs::FS& fs = storage::data();
  String error;
  File f = fs.open(WebServer::kRestorePath, "r");
  if (engine_.isActive() || crosstalk_.active()) {
    error = "หยุดเกมหรือการจูนก่อนกู้คืน";
  } else if (!f) {
    error = "ไม่พบไฟล์ที่อัปโหลด";
  } else {
    JsonDocument doc;
    const String header = f.readStringUntil('\n');
    HubSettings restored = settings_;
    uint16_t games = 0;
    if (deserializeJson(doc, header) != DeserializationError::Ok || doc["t"] != kBackupMagic ||
        doc["v"].as<int>() != 1) {
      error = "ไม่ใช่ไฟล์สำรองของ Target Range";
    } else if (!restored.applyAll(doc["settings"].as<JsonObjectConst>(), true, error)) {
      error = "ค่าตั้งในไฟล์ไม่ถูกต้อง: " + error;
    } else if (history_.replaceFrom(f, games, error)) {
      // History is already replaced, so always reboot: everything (Wi-Fi, engine,
      // node configs) then comes up from what is actually on flash.
      rebootAtMs_ = millis() + kRestoreRebootMs;
      if (restored.save()) {
        toAdmins(msg::ok("restore", String("กู้คืนแล้ว: ประวัติ ") + games + " เกม และค่าตั้งทั้งหมด hub จะรีสตาร์ท"));
      } else {
        error = "กู้ประวัติแล้ว แต่บันทึกค่าตั้งไม่สำเร็จ hub จะรีสตาร์ทด้วยค่าตั้งเดิม";
      }
    }
  }
  if (f) f.close();
  fs.remove(WebServer::kRestorePath);
  if (error.length() > 0) toAdmins(msg::error("restore", "กู้คืนไม่สำเร็จ: " + error));
}

// ---------------------------------------------------------------- commands

void App::onCommand(uint32_t client, JsonObjectConst cmd, uint32_t nowMs) {
  const char* type = cmd["t"];

  if (strcmp(type, "time") == 0) {
    // Phones know the wall-clock time; the hub has no RTC. Used to stamp history.
    const double epoch = cmd["epoch_ms"] | 0.0;
    if (epoch > 1.6e12) {
      epochOffsetMs_ = static_cast<int64_t>(epoch) - static_cast<int64_t>(nowMs);
      hasClock_ = true;
    }
  } else if (strcmp(type, "auth") == 0) {
    const bool ok = settings_.adminPin == (cmd["pin"] | "");
    if (ok && !isAdmin(client)) admins_.push_back(client);
    reply(client, msg::authResult(ok));
    if (ok) {
      reply(client, msg::settings(settings_));
      reply(client, msg::nodesDetail(registry_, settings_));
      reply(client, msg::storageStatus(storage::dataUsage(), history_.count(), HistoryStore::kKeep, archive_.status()));
    }
  } else if (strcmp(type, "start") == 0) {
    startGame(client, cmd, nowMs);
  } else if (strcmp(type, "stop") == 0) {
    engine_.stop(nowMs);
  } else if (strcmp(type, "resume") == 0) {
    if (!registry_.allOnline()) {
      reply(client, msg::error("node_offline", "รอให้หุ่นทั้งสองออนไลน์ก่อน"));
    } else {
      engine_.resume(nowMs);
    }
  } else if (strcmp(type, "reset") == 0) {
    engine_.reset();
  } else if (isAdmin(client)) {
    onAdminCommand(client, type, cmd, nowMs);
  } else {
    reply(client, msg::error("auth_required", "ต้องใส่ PIN ผู้ดูแลก่อน"));
  }
}

void App::startGame(uint32_t client, JsonObjectConst cmd, uint32_t nowMs) {
  if (crosstalk_.active()) {
    reply(client, msg::error("busy", "กำลังจูนเซนเซอร์อยู่"));
    return;
  }
  for (uint8_t node = 1; node <= kLaneCount; ++node) {
    if (!registry_.node(node).online) {
      reply(client, msg::error("node_offline", String("หุ่น ") + node + " ยังไม่ออนไลน์"));
      return;
    }
  }

  StartRequest req;
  req.mode = (cmd["mode"] | 1) == 2 ? GameMode::Duel : GameMode::Solo;
  req.difficulty = static_cast<Difficulty>(cmd["difficulty"] | 1);
  req.durationS = cmd["duration_s"] | 60;
  JsonArrayConst names = cmd["players"].as<JsonArrayConst>();
  for (uint8_t p = 0; p < kMaxPlayers; ++p) {
    req.names[p] = cleanName(p < names.size() ? names[p].as<const char*>() : nullptr, p).c_str();
  }
  for (uint8_t l = 0; l < kLaneCount; ++l) req.zoneMask[l] = settings_.nodes[l].zonesEnabled;
  req.seed = esp_random();

  for (uint8_t node = 1; node <= kLaneCount; ++node) {
    if (tuningMask_ & (1u << node)) setTuning(node, false);
  }
  const StartError err = engine_.start(req, nowMs);
  if (err != StartError::None) reply(client, msg::error(toString(err), startErrorText(err)));
}

void App::onAdminCommand(uint32_t client, const char* type, JsonObjectConst cmd, uint32_t nowMs) {
  uint8_t node = 0;
  const bool idle = !engine_.isActive();
  String error;

  if (strcmp(type, "get_settings") == 0) {
    reply(client, msg::settings(settings_));
  } else if (strcmp(type, "led_test") == 0) {
    const long zone = cmd["zone"] | -1L;
    if (!validNode(cmd, node) || zone < 0 || zone >= kZoneCount || !idle) {
      reply(client, msg::error("bad_request", "ทดสอบไฟได้เฉพาะตอนไม่มีเกม"));
      return;
    }
    nodeLeds(node, static_cast<uint8_t>(1u << zone), kOff, LedColor::Green, kLedTestMs);
  } else if (strcmp(type, "identify") == 0 || strcmp(type, "calibrate") == 0 || strcmp(type, "reboot_node") == 0) {
    if (!validNode(cmd, node)) return reply(client, msg::error("bad_request", "ไม่มีหุ่นหมายเลขนี้"));
    const proto::CommandOp op = strcmp(type, "identify") == 0    ? proto::CommandOp::Identify
                                : strcmp(type, "calibrate") == 0 ? proto::CommandOp::Calibrate
                                                                 : proto::CommandOp::Reboot;
    link_.sendCommand(node, op);
    reply(client, msg::ok(type));
  } else if (strcmp(type, "sound_test") == 0) {
    if (!validNode(cmd, node)) return reply(client, msg::error("bad_request", "ไม่มีหุ่นหมายเลขนี้"));
    if (!registry_.node(node).online) {
      return reply(client, msg::error("node_offline", String("หุ่น ") + node + " ยังไม่ออนไลน์"));
    }
    playHitSound(node);
    reply(client, msg::ok(type, String("สั่งหุ่น ") + node + " เล่นเสียงแล้ว"));
  } else if (strcmp(type, "tune") == 0) {
    if (!validNode(cmd, node) || !idle) return reply(client, msg::error("bad_request", "จูนได้เฉพาะตอนไม่มีเกม"));
    setTuning(node, cmd["on"] | false);
  } else if (strcmp(type, "crosstalk_start") == 0) {
    if (!validNode(cmd, node) || !idle) return reply(client, msg::error("bad_request", "ทดสอบได้เฉพาะตอนไม่มีเกม"));
    startCrosstalk(client, node, static_cast<uint8_t>(constrain(cmd["shots"] | 5, 1, 20)));
  } else if (strcmp(type, "crosstalk_cancel") == 0) {
    stopCrosstalk();
  } else if (strcmp(type, "apply_suggested") == 0) {
    if (!validNode(cmd, node) || !hasReport_ || crosstalk_.nodeId() != node) {
      return reply(client, msg::error("bad_request", "ยังไม่มีผลทดสอบของหุ่นนี้"));
    }
    settings_.nodes[nodeIdToLane(node)].threshold = lastReport_.suggested;
    if (persist(client)) {
      pushConfig(node);
      reply(client, msg::ok(type, "ใช้ค่า threshold ที่แนะนำแล้ว"));
      reply(client, msg::settings(settings_));
    }
  } else if (strcmp(type, "sim_hit") == 0) {
    const long zone = cmd["zone"] | -1L;
    if (!validNode(cmd, node) || zone < 0 || zone >= kZoneCount) return;
    if (engine_.state() != GameState::Running) return reply(client, msg::error("not_running", "ยังไม่ได้เริ่มเกม"));
    engine_.onHit(nodeIdToLane(node), static_cast<uint8_t>(zone), nowMs, nowMs);
  } else if (strcmp(type, "save_game") == 0) {
    if (!idle) return reply(client, msg::error("busy", "แก้ค่าเกมได้เฉพาะตอนไม่มีเกม"));
    if (!settings_.applyGame(cmd["game"], error)) return reply(client, msg::error("invalid", error));
    engine_.setSettings(settings_.game);
    if (persist(client)) {
      reply(client, msg::ok(type, "บันทึกค่าเกมแล้ว"));
      web_.broadcast(msg::publicConfig(settings_));
    }
  } else if (strcmp(type, "save_hardware") == 0) {
    if (!settings_.applyHardware(cmd["hardware"], error)) return reply(client, msg::error("invalid", error));
    if (persist(client)) {
      for (uint8_t n = 1; n <= kLaneCount; ++n) pushConfig(n);
      reply(client, msg::ok(type, "บันทึกและส่งค่าไปที่หุ่นแล้ว"));
    }
  } else if (strcmp(type, "save_node") == 0) {
    if (!validNode(cmd, node)) return reply(client, msg::error("bad_request", "ไม่มีหุ่นหมายเลขนี้"));
    if (!settings_.applyNodeTuning(nodeIdToLane(node), cmd, error)) return reply(client, msg::error("invalid", error));
    if (persist(client)) {
      pushConfig(node);
      reply(client, msg::ok(type, String("บันทึก threshold หุ่น ") + node + " แล้ว"));
    }
  } else if (strcmp(type, "save_wifi") == 0) {
    if (!settings_.applyWifi(cmd["wifi"], error)) return reply(client, msg::error("invalid", error));
    if (persist(client)) reply(client, msg::ok(type, "บันทึกแล้ว รีสตาร์ท hub เพื่อใช้ค่า WiFi ใหม่"));
  } else if (strcmp(type, "set_pin") == 0) {
    if (!settings_.applyPin(cmd["pin"] | "", error)) return reply(client, msg::error("invalid", error));
    if (persist(client)) reply(client, msg::ok(type, "เปลี่ยน PIN แล้ว"));
  } else if (strcmp(type, "reboot_hub") == 0) {
    reply(client, msg::ok(type, "กำลังรีสตาร์ท hub"));
    rebootAtMs_ = nowMs + 500;
  } else {
    reply(client, msg::error("unknown", String("ไม่รู้จักคำสั่ง ") + type));
  }
}

// ---------------------------------------------------------------- node traffic

void App::onHello(uint8_t node, const proto::HelloMsg& m) {
  const uint32_t nowMs = millis();
  registry_.onHello(node, m, nowMs);
  ++nodesRev_;
  pushConfig(node);
  pushDesiredLeds(node, kAllZonesMask);
  if (tuningMask_ & (1u << node)) link_.sendCommand(node, proto::CommandOp::TuneOn);
  // A node that rebooted mid-game is caught by the heartbeat timeout; nothing else to do here.
}

void App::onHit(uint8_t node, const proto::HitMsg& m, uint32_t nowMs) {
  registry_.touch(node, nowMs);
  if (m.zone >= kZoneCount) return;
  if (!admins_.empty()) toAdmins(msg::peaks(node, m.zone, m.peaks, true));
  if (crosstalk_.active()) return;  // tuning works on Peaks frames
  engine_.onHit(nodeIdToLane(node), m.zone, nowMs - m.ageMs, nowMs);
}

void App::onHeartbeat(uint8_t node, const proto::HeartbeatMsg& m, int8_t rssi) {
  const bool wasOnline = registry_.node(node).online;
  const uint8_t repair = registry_.onHeartbeat(node, m, rssi, millis());
  if (!wasOnline) ++nodesRev_;
  if (repair != 0) pushDesiredLeds(node, repair);
}

void App::onPeaks(uint8_t node, const proto::PeaksMsg& m) {
  if (m.zone >= kZoneCount) return;
  toAdmins(msg::peaks(node, m.zone, m.peaks, false));
  if (!crosstalk_.active() || crosstalk_.nodeId() != node) return;

  std::array<uint16_t, kZoneCount> peaks{};
  for (uint8_t z = 0; z < kZoneCount; ++z) peaks[z] = m.peaks[z];
  const uint8_t struck = crosstalk_.zone();
  const bool advanced = crosstalk_.record(peaks);
  nodeLeds(node, static_cast<uint8_t>(1u << struck), advanced ? kOff : kCrosstalkTarget, LedColor::Blue, 150);

  if (crosstalk_.done()) {
    lastReport_ = crosstalk_.report(registry_.node(node).noise);
    hasReport_ = true;
    setTuning(node, false);
    nodeLeds(node, kAllZonesMask, kOff, LedColor::Green, 1000);
    toAdmins(msg::crosstalk(crosstalk_, &lastReport_));
    ++nodesRev_;
    return;
  }
  if (advanced) lightCrosstalkZone();
  toAdmins(msg::crosstalk(crosstalk_, nullptr));
}

void App::pushConfig(uint8_t node) {
  proto::ConfigMsg c = {};
  const NodeTuning& t = settings_.nodes[nodeIdToLane(node)];
  for (uint8_t z = 0; z < kZoneCount; ++z) c.threshold[z] = t.threshold[z];
  c.lockoutMs = settings_.hardware.lockoutMs;
  c.arbiterWindowMs = settings_.hardware.arbiterWindowMs;
  c.brightness = settings_.hardware.brightness;
  c.colorOrder = settings_.hardware.colorOrder;
  c.ledProtocol = settings_.hardware.ledProtocol;
  c.volume = settings_.hardware.volume;
  link_.sendConfig(node, c);
}

void App::pushDesiredLeds(uint8_t node, uint8_t zoneMask) {
  // Group zones that share a look so the node gets as few commands as possible.
  const NodeStatus& n = registry_.node(node);
  uint8_t remaining = zoneMask & kAllZonesMask;
  while (remaining != 0) {
    uint8_t first = 0;
    while ((remaining & (1u << first)) == 0) ++first;
    const LedSteady look = n.desired[first];
    uint8_t group = 0;
    for (uint8_t z = first; z < kZoneCount; ++z) {
      if ((remaining & (1u << z)) && n.desired[z].sameLook(look)) group |= static_cast<uint8_t>(1u << z);
    }
    link_.sendLed(node, group, look, LedColor::Off, 0);
    remaining &= static_cast<uint8_t>(~group);
  }
}

void App::nodeLeds(uint8_t node, uint8_t zoneMask, const LedSteady& steady, LedColor flash, uint16_t flashMs) {
  registry_.setDesired(node, zoneMask, steady);
  if (registry_.node(node).online) link_.sendLed(node, zoneMask, steady, flash, flashMs);
}

void App::setTuning(uint8_t node, bool on) {
  if (on) {
    tuningMask_ |= static_cast<uint8_t>(1u << node);
  } else {
    tuningMask_ &= static_cast<uint8_t>(~(1u << node));
  }
  link_.sendCommand(node, on ? proto::CommandOp::TuneOn : proto::CommandOp::TuneOff);
}

void App::startCrosstalk(uint32_t client, uint8_t node, uint8_t shots) {
  if (!registry_.node(node).online) {
    reply(client, msg::error("node_offline", String("หุ่น ") + node + " ยังไม่ออนไลน์"));
    return;
  }
  if (crosstalk_.active()) stopCrosstalk();
  crosstalk_.start(node, shots);
  hasReport_ = false;
  setTuning(node, true);
  lightCrosstalkZone();
  ++nodesRev_;
  toAdmins(msg::crosstalk(crosstalk_, nullptr));
}

void App::stopCrosstalk() {
  const uint8_t node = crosstalk_.nodeId();
  crosstalk_.cancel();
  setTuning(node, false);
  nodeLeds(node, kAllZonesMask, kOff);
  ++nodesRev_;
  toAdmins(msg::crosstalk(crosstalk_, nullptr));
}

void App::lightCrosstalkZone() {
  const uint8_t node = crosstalk_.nodeId();
  const uint8_t zoneBit = static_cast<uint8_t>(1u << crosstalk_.zone());
  nodeLeds(node, static_cast<uint8_t>(kAllZonesMask & ~zoneBit), kOff);
  nodeLeds(node, zoneBit, kCrosstalkTarget);
}

// ---------------------------------------------------------------- GameListener

void App::setLaneLeds(uint8_t lane, uint8_t zoneMask, const LedSteady& steady, LedColor flash, uint16_t flashMs) {
  nodeLeds(laneToNodeId(lane), zoneMask, steady, flash, flashMs);
}

void App::onGameEvent(const GameEvent& event) {
  if (event.kind == game::EventKind::Hit) playHitSound(laneToNodeId(event.lane));
  web_.broadcast(msg::event(event));
}

void App::playHitSound(uint8_t node) {
  if (!registry_.node(node).online) return;
  const uint8_t track = static_cast<uint8_t>(1 + esp_random() % settings_.hardware.hitTracks);
  link_.sendSound(node, kHitSoundFolder, track);
}

void App::onGameFinished(const GameResult& result) {
  const uint32_t id = history_.nextId();
  const uint64_t ts = epochNow(millis());
  JsonDocument doc;
  msg::result(doc.to<JsonObject>(), result, id, ts);
  String line;
  serializeJson(doc, line);
  if (storageOk_ && !history_.append(line)) {
    web_.broadcast(msg::error("storage", "บันทึกประวัติไม่สำเร็จ"));
  }
  archive_.submit(line);
  web_.broadcast(msg::resultMessage(result, id, ts));
}

}  // namespace tr::hub
