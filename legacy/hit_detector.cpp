#include "hit_detector.h"
#include <math.h>

namespace {
constexpr uint32_t SAMPLE_INTERVAL_US = 500;      // target: ~2 kHz
constexpr uint32_t CALIBRATION_DELAY_US = 500;
constexpr uint32_t CALIBRATION_SAMPLES = 1000;   // ~500 ms
constexpr uint32_t PEAK_WINDOW_US = 20000;       // collect peak for 20 ms
constexpr uint32_t LOCKOUT_US = 150000;          // minimum gap between hits
constexpr uint32_t RELEASE_STABLE_US = 30000;    // signal must stay quiet 30 ms
constexpr int HIT_THRESHOLD = 100;               // tune after safe tests
constexpr int RELEASE_THRESHOLD = 45;
constexpr float BASELINE_ALPHA = 0.001f;
constexpr int PIEZO_PIN_FOR_OUTPUT = 34;
constexpr int TARGET_ID = 1;
}

HitDetector::HitDetector(int pin) : pin_(pin) {}

void HitDetector::begin() {
    pinMode(pin_, INPUT);
    analogReadResolution(12);
    calibrate();
    lastSampleUs_ = micros();
}

int HitDetector::readAdc() {
    return analogRead(pin_);
}

void HitDetector::calibrate() {
    Serial.println("# Calibrating baseline...");
    uint64_t total = 0;

    for (uint32_t i = 0; i < CALIBRATION_SAMPLES; ++i) {
        total += static_cast<uint32_t>(readAdc());
        delayMicroseconds(CALIBRATION_DELAY_US);
    }

    baseline_ = static_cast<float>(total) /
                static_cast<float>(CALIBRATION_SAMPLES);
    calibrated_ = true;

    Serial.printf("# Baseline ready: %.2f\n", static_cast<double>(baseline_));
    Serial.println("# Ready; JSON HIT events follow.");
}

float HitDetector::baseline() const {
    return baseline_;
}

uint32_t HitDetector::hitCount() const {
    return hitCount_;
}

void HitDetector::update(uint32_t nowUs) {
    if (!calibrated_) return;

    if (static_cast<uint32_t>(nowUs - lastSampleUs_) < SAMPLE_INTERVAL_US) {
        return;
    }

    lastSampleUs_ = nowUs;
    processSample(readAdc(), nowUs);
}

void HitDetector::processSample(int adc, uint32_t nowUs) {
    const int delta = static_cast<int>(
        fabsf(static_cast<float>(adc) - baseline_)
    );

    switch (state_) {
        case State::IDLE: {
            const bool lockoutPassed =
                hitCount_ == 0 ||
                static_cast<uint32_t>(nowUs - lastHitUs_) >= LOCKOUT_US;

            if (delta >= HIT_THRESHOLD && lockoutPassed) {
                state_ = State::CAPTURING_PEAK;
                stateStartUs_ = nowUs;
                peakAdc_ = adc;
                peakDelta_ = delta;
            } else if (delta < HIT_THRESHOLD && lockoutPassed) {
                baseline_ += BASELINE_ALPHA *
                    (static_cast<float>(adc) - baseline_);
            }
            break;
        }

        case State::CAPTURING_PEAK:
            if (delta > peakDelta_) {
                peakDelta_ = delta;
                peakAdc_ = adc;
            }

            if (static_cast<uint32_t>(nowUs - stateStartUs_) >= PEAK_WINDOW_US) {
                if (peakDelta_ >= HIT_THRESHOLD) {
                    emitHit(nowUs);
                }
                state_ = State::WAIT_FOR_RELEASE;
                releaseStartUs_ = 0;
            }
            break;

        case State::WAIT_FOR_RELEASE:
            // Do not count another event until the piezo signal has stayed quiet.
            if (delta <= RELEASE_THRESHOLD) {
                if (releaseStartUs_ == 0) {
                    releaseStartUs_ = nowUs;
                } else if (static_cast<uint32_t>(nowUs - releaseStartUs_) >= RELEASE_STABLE_US) {
                    const bool lockoutPassed =
                        static_cast<uint32_t>(nowUs - lastHitUs_) >= LOCKOUT_US;
                    if (lockoutPassed) {
                        state_ = State::IDLE;
                        releaseStartUs_ = 0;
                    }
                }
            } else {
                releaseStartUs_ = 0;
            }
            break;
    }
}

void HitDetector::emitHit(uint32_t nowUs) {
    ++hitCount_;
    lastHitUs_ = nowUs;

    // Keep each event on one line so the dashboard can parse JSONL.
    Serial.printf(
        "{\"event\":\"HIT\",\"target_id\":%d,\"hit_count\":%lu,"
        "\"timestamp_ms\":%lu,\"peak_adc\":%d,\"peak_delta\":%d,"
        "\"baseline\":%.2f}\n",
        TARGET_ID,
        static_cast<unsigned long>(hitCount_),
        static_cast<unsigned long>(nowUs / 1000U),
        peakAdc_,
        peakDelta_,
        static_cast<double>(baseline_)
    );
}
