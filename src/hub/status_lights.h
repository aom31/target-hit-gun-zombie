#pragma once

// Three panel LEDs on the hub box: green = ready, yellow = waiting, red = fault.

#include <Arduino.h>

namespace tr::hub {

class StatusLights {
 public:
  enum class Mode : uint8_t { Booting, WaitingForNodes, Ready, Fault };

  void begin(uint8_t greenPin, uint8_t yellowPin, uint8_t redPin);
  void set(Mode mode) { mode_ = mode; }
  void update(uint32_t nowMs);

 private:
  uint8_t green_ = 0, yellow_ = 0, red_ = 0;
  Mode mode_ = Mode::Booting;
};

}  // namespace tr::hub
