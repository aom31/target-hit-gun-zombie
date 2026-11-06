#include "sensor_task.h"

namespace tr::io {

namespace {
constexpr uint32_t kStackBytes = 4096;
constexpr UBaseType_t kPriority = configMAX_PRIORITIES - 2;
constexpr BaseType_t kCore = 1;  // Wi-Fi lives on core 0
constexpr uint8_t kQueueDepth = 8;
constexpr uint32_t kPublishEveryFrames = 200;  // ~10 Hz
}  // namespace

bool SensorTask::begin(const std::array<uint8_t, kZoneCount>& pins, const sensing::ArbiterConfig& cfg) {
  pins_ = pins;
  analogReadResolution(12);
  for (uint8_t pin : pins_) {
    pinMode(pin, INPUT);
    analogSetPinAttenuation(pin, ADC_11db);
  }
  arbiter_.configure(cfg);

  queue_ = xQueueCreate(kQueueDepth, sizeof(sensing::Detection));
  if (queue_ == nullptr) return false;
  if (xTaskCreatePinnedToCore(taskEntry, "sensor", kStackBytes, this, kPriority, &task_, kCore) != pdPASS) {
    return false;
  }

  esp_timer_create_args_t args = {};
  args.callback = &SensorTask::timerTick;
  args.arg = this;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "sensor_tick";
  if (esp_timer_create(&args, &timer_) != ESP_OK) return false;
  return esp_timer_start_periodic(timer_, kFramePeriodUs) == ESP_OK;
}

void SensorTask::configure(const sensing::ArbiterConfig& cfg) {
  portENTER_CRITICAL(&mux_);
  pendingConfig_ = cfg;
  configPending_ = true;
  portEXIT_CRITICAL(&mux_);
}

SensorStats SensorTask::stats() const {
  portENTER_CRITICAL(&mux_);
  SensorStats copy = published_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}

void SensorTask::taskEntry(void* arg) { static_cast<SensorTask*>(arg)->run(); }

void SensorTask::timerTick(void* arg) {
  auto* self = static_cast<SensorTask*>(arg);
  if (self->task_ != nullptr) xTaskNotifyGive(self->task_);
}

void SensorTask::run() {
  std::array<uint16_t, kZoneCount> frame{};
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Apply requests from other tasks between frames.
    portENTER_CRITICAL(&mux_);
    const bool hasConfig = configPending_;
    const sensing::ArbiterConfig cfg = pendingConfig_;
    configPending_ = false;
    portEXIT_CRITICAL(&mux_);
    if (hasConfig) arbiter_.configure(cfg);

    const int8_t tune = tuningRequest_.exchange(-1);
    if (tune >= 0) arbiter_.setTuning(tune == 1);
    if (calibrationRequested_.exchange(false)) arbiter_.startCalibration(kCalibrationFrames);
    arbiter_.guardUntil(guardUntilUs_.load());

    for (uint8_t ch = 0; ch < kZoneCount; ++ch) frame[ch] = static_cast<uint16_t>(analogRead(pins_[ch]));
    const uint32_t nowUs = static_cast<uint32_t>(esp_timer_get_time());

    sensing::Detection detection;
    if (arbiter_.process(frame, nowUs, detection)) {
      ++detections_;
      xQueueSend(queue_, &detection, 0);  // drop if the link task is far behind
    }

    if (++framesSincePublish_ >= kPublishEveryFrames) {
      framesSincePublish_ = 0;
      publishStats();
    }
  }
}

void SensorTask::publishStats() {
  SensorStats s;
  for (uint8_t ch = 0; ch < kZoneCount; ++ch) {
    s.baseline[ch] = arbiter_.baseline(ch);
    s.noise[ch] = arbiter_.noise(ch);
    s.trigger[ch] = arbiter_.trigger(ch);
  }
  s.calibrated = arbiter_.calibrated();
  s.tuning = arbiter_.tuning();
  s.detections = detections_;
  portENTER_CRITICAL(&mux_);
  published_ = s;
  portEXIT_CRITICAL(&mux_);
}

}  // namespace tr::io
