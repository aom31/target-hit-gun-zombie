#pragma once

// Fire-and-forget driver for the DFPlayer Mini MP3 module (UART, 9600 baud).
//
// The module needs ~1.5 s after power-up before it accepts commands, so
// commands issued earlier are held and sent once it is ready. A new play
// command interrupts whatever is playing, which is what rapid hits want.
//
// The module silently ignores a command that arrives too soon after the
// previous one, so commands are spaced kCommandGapMs apart. Only the newest
// play request is kept: a hit never waits for an older hit's sound.

#include <Arduino.h>

namespace tr::io {

class Mp3Player {
 public:
  bool begin(HardwareSerial& serial, int8_t rxPin, int8_t txPin);
  void setVolume(uint8_t volume);  // 0..30
  void play(uint8_t folder, uint8_t track);
  void stop();
  void poll(uint32_t nowMs);  // flushes held commands once the module is ready

 private:
  static constexpr uint32_t kBootMs = 1500;
  static constexpr uint32_t kCommandGapMs = 30;

  void sendCommand(uint8_t cmd, uint16_t param);
  bool ready(uint32_t nowMs) const { return started_ && nowMs - startedAtMs_ >= kBootMs; }
  bool canSend(uint32_t nowMs) const { return ready(nowMs) && (!sentAny_ || nowMs - lastSendMs_ >= kCommandGapMs); }

  HardwareSerial* serial_ = nullptr;
  bool started_ = false;
  uint32_t startedAtMs_ = 0;
  int16_t pendingVolume_ = -1;
  uint16_t pendingPlay_ = 0;  // (folder << 8) | track, 0 = none
  bool sentAny_ = false;
  uint32_t lastSendMs_ = 0;
};

}  // namespace tr::io
