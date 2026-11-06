#include "status_lights.h"

namespace tr::hub {

void StatusLights::begin(uint8_t greenPin, uint8_t yellowPin, uint8_t redPin) {
  green_ = greenPin;
  yellow_ = yellowPin;
  red_ = redPin;
  for (uint8_t pin : {green_, yellow_, red_}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
}

void StatusLights::update(uint32_t nowMs) {
  const bool blink = (nowMs / 250) % 2 == 0;
  digitalWrite(green_, mode_ == Mode::Ready ? HIGH : LOW);
  digitalWrite(yellow_, mode_ == Mode::WaitingForNodes || (mode_ == Mode::Booting && blink) ? HIGH : LOW);
  digitalWrite(red_, mode_ == Mode::Fault ? HIGH : LOW);
}

}  // namespace tr::hub
