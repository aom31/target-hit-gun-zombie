#include <Arduino.h>
#include "hit_detector.h"

namespace {
constexpr int PIEZO_PIN = 34;
HitDetector detector(PIEZO_PIN);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("# Target Hit Detector");
    detector.begin();
}

void loop() {
    detector.update(micros());
}
