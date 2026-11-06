// Mannequin node firmware: piezo sensing + zone lights, controlled by the hub.

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

#include "mp3_player.h"
#include "node_link.h"
#include "node_pins.h"
#include "node_settings.h"
#include "sensor_task.h"
#include "zone_leds.h"

using namespace tr;

namespace {

constexpr uint32_t kLedGuardUs = 3000;       // ignore piezo input right after the lights change
constexpr uint32_t kCalibrationWaitMs = 2000;
constexpr uint32_t kIdentifyMs = 3000;
constexpr uint32_t kWatchdogS = 5;

node::NodeSettings settings;
io::ZoneLeds leds;
io::SensorTask sensor;
io::Mp3Player player;
node::NodeLink radio;
uint8_t nodeId = 1;
bool configured = false;
bool rebootRequested = false;

uint8_t readNodeId() {
  pinMode(pins::kNodeIdJumper, INPUT_PULLUP);
  delay(5);
  return digitalRead(pins::kNodeIdJumper) == LOW ? 2 : 1;
}

void applyHardwareSettings() {
  leds.configure(settings.brightness, settings.colorOrder, settings.ledProtocol);
  sensor.configure(settings.arbiterConfig());
  player.setVolume(settings.volume);
}

void fillHeartbeat(proto::HeartbeatMsg& m) {
  const io::SensorStats s = sensor.stats();
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    m.baseline[z] = s.baseline[z];
    m.noise[z] = s.noise[z];
    m.threshold[z] = settings.threshold[z];
    m.ledState[z] = leds.steady()[z].packed();
  }
  m.flags = static_cast<uint8_t>((s.tuning ? proto::kFlagTuning : 0) | (s.calibrated ? proto::kFlagCalibrated : 0) |
                                 (configured ? proto::kFlagConfigured : 0));
  m.hitCount = s.detections;
}

void onCommand(proto::CommandOp op) {
  switch (op) {
    case proto::CommandOp::Calibrate:
      sensor.requestCalibration();
      break;
    case proto::CommandOp::TuneOn:
      sensor.setTuning(true);
      break;
    case proto::CommandOp::TuneOff:
      sensor.setTuning(false);
      break;
    case proto::CommandOp::Identify:
      leds.setOverlay(io::ZoneLeds::Overlay::Identify, millis() + kIdentifyMs);
      break;
    case proto::CommandOp::Reboot:
      rebootRequested = true;  // after the ack has gone out
      break;
  }
}

void updateOverlay(uint32_t nowMs) {
  const auto current = leds.overlay();
  if (current == io::ZoneLeds::Overlay::Identify) return;
  if (!radio.everPaired()) {
    leds.setOverlay(io::ZoneLeds::Overlay::Searching);
  } else if (!radio.linkUp(nowMs)) {
    // Never leave a target lit when the hub is gone: players would shoot at nothing.
    if (current != io::ZoneLeds::Overlay::LinkLost) leds.allOff(nowMs);
    leds.setOverlay(io::ZoneLeds::Overlay::LinkLost);
  } else {
    leds.setOverlay(io::ZoneLeds::Overlay::None);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(pins::kOnboardLed, OUTPUT);
  nodeId = readNodeId();
  Serial.printf("# Target Range node %u, firmware %s\n", nodeId, kFirmwareVersion);

  settings.load();
  leds.begin(pins::kPixelData, pins::kPixelsPerZone, settings.ledProtocol);
  leds.configure(settings.brightness, settings.colorOrder, settings.ledProtocol);
  leds.render(millis());
  leds.onFrameSent([] { sensor.guardFor(kLedGuardUs); });
  player.begin(Serial2, pins::kMp3Rx, pins::kMp3Tx);
  player.setVolume(settings.volume);

  // Calibrates the piezo baseline on start-up: keep the mannequin still for half a second.
  if (!sensor.begin(pins::kPiezo, settings.arbiterConfig())) {
    Serial.println("# sensor task failed to start");
  }
  const uint32_t calStart = millis();
  while (!sensor.stats().calibrated && millis() - calStart < kCalibrationWaitMs) delay(20);

  node::NodeLink::Handlers h;
  h.onLed = [](const proto::LedMsg& m) {
    leds.apply(m.zoneMask, LedSteady{m.color, m.fx, m.periodMs}, m.flashColor, m.flashMs, millis());
  };
  h.onConfig = [](const proto::ConfigMsg& m) {
    if (!settings.applyFrom(m)) return;
    settings.save();
    applyHardwareSettings();
    configured = true;
  };
  h.onCommand = onCommand;
  h.onSound = [](const proto::SoundMsg& m) { player.play(m.folder, m.track); };
  h.fillHeartbeat = fillHeartbeat;
  if (!radio.begin(nodeId, h)) Serial.println("# ESP-NOW init failed");

  esp_task_wdt_config_t wdt = {};
  wdt.timeout_ms = kWatchdogS * 1000;
  wdt.trigger_panic = true;
  esp_task_wdt_reconfigure(&wdt);
  esp_task_wdt_add(nullptr);
  Serial.println("# ready");
}

void loop() {
  const uint32_t nowMs = millis();
  radio.poll(nowMs);

  sensing::Detection d;
  const bool tuning = sensor.stats().tuning;
  while (sensor.nextDetection(d)) {
    const uint32_t nowUs = static_cast<uint32_t>(esp_timer_get_time());
    const uint32_t agoMs = (nowUs - d.atUs) / 1000;
    if (tuning) {
      radio.sendPeaks(d);
    } else {
      radio.sendHit(d, agoMs, nowMs);
    }
    Serial.printf("{\"event\":\"HIT\",\"target_id\":%u,\"zone_id\":%u,\"peaks\":[%u,%u,%u,%u,%u]}\n", nodeId,
                  d.zone + 1, d.peaks[0], d.peaks[1], d.peaks[2], d.peaks[3], d.peaks[4]);
  }

  player.poll(nowMs);
  updateOverlay(nowMs);
  leds.render(nowMs);
  digitalWrite(pins::kOnboardLed, radio.linkUp(nowMs) ? HIGH : LOW);
  esp_task_wdt_reset();

  if (rebootRequested) {
    delay(100);
    ESP.restart();
  }
  delay(2);
}
