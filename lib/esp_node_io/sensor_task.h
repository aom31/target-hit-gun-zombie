#pragma once

// Samples the five piezo inputs at a fixed rate on its own FreeRTOS task and
// runs the hit arbiter. Other tasks talk to it only through the thread-safe
// methods below; detections come out of a queue.

#include <Arduino.h>
#include <esp_timer.h>

#include <array>
#include <atomic>

#include "hit_arbiter.h"

namespace tr::io {

struct SensorStats {
  std::array<uint16_t, kZoneCount> baseline{};
  std::array<uint16_t, kZoneCount> noise{};
  std::array<uint16_t, kZoneCount> trigger{};
  bool calibrated = false;
  bool tuning = false;
  uint32_t detections = 0;
};

class SensorTask {
 public:
  static constexpr uint32_t kFramePeriodUs = 500;  // 2 kHz per channel
  static constexpr uint16_t kCalibrationFrames = 1000;

  bool begin(const std::array<uint8_t, kZoneCount>& pins, const sensing::ArbiterConfig& cfg);

  void configure(const sensing::ArbiterConfig& cfg);
  void setTuning(bool on) { tuningRequest_.store(on ? 1 : 0); }
  void requestCalibration() { calibrationRequested_.store(true); }
  void guardFor(uint32_t us) { guardUntilUs_.store(static_cast<uint32_t>(esp_timer_get_time()) + us); }

  bool nextDetection(sensing::Detection& out) { return xQueueReceive(queue_, &out, 0) == pdTRUE; }
  SensorStats stats() const;

 private:
  static void taskEntry(void* arg);
  static void timerTick(void* arg);
  void run();
  void publishStats();

  std::array<uint8_t, kZoneCount> pins_{};
  sensing::HitArbiter arbiter_;
  TaskHandle_t task_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  esp_timer_handle_t timer_ = nullptr;

  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  sensing::ArbiterConfig pendingConfig_;
  bool configPending_ = false;
  SensorStats published_;

  std::atomic<int8_t> tuningRequest_{-1};
  std::atomic<bool> calibrationRequested_{true};
  std::atomic<uint32_t> guardUntilUs_{0};
  uint32_t detections_ = 0;
  uint32_t framesSincePublish_ = 0;
};

}  // namespace tr::io
