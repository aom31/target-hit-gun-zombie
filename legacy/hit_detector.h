#pragma once

#include <Arduino.h>

class HitDetector {
public:
    explicit HitDetector(int pin);

    void begin();
    void update(uint32_t nowUs);
    float baseline() const;
    uint32_t hitCount() const;

private:
    enum class State : uint8_t {
        IDLE,
        CAPTURING_PEAK,
        WAIT_FOR_RELEASE
    };

    int pin_;
    State state_ = State::IDLE;
    float baseline_ = 0.0f;
    bool calibrated_ = false;

    uint32_t stateStartUs_ = 0;
    uint32_t lastSampleUs_ = 0;
    uint32_t releaseStartUs_ = 0;
    uint32_t lastHitUs_ = 0;
    uint32_t hitCount_ = 0;

    int peakAdc_ = 0;
    int peakDelta_ = 0;

    int readAdc();
    void calibrate();
    void processSample(int adc, uint32_t nowUs);
    void emitHit(uint32_t nowUs);
};
