#include "mp3_player.h"

namespace tr::io {

namespace {
constexpr uint32_t kBaud = 9600;
constexpr uint8_t kCmdVolume = 0x06;
constexpr uint8_t kCmdStop = 0x16;
constexpr uint8_t kCmdPlayFolderTrack = 0x0F;
constexpr uint8_t kMaxVolume = 30;
}  // namespace

bool Mp3Player::begin(HardwareSerial& serial, int8_t rxPin, int8_t txPin) {
  serial_ = &serial;
  serial_->begin(kBaud, SERIAL_8N1, rxPin, txPin);
  started_ = true;
  startedAtMs_ = millis();
  return true;
}

void Mp3Player::setVolume(uint8_t volume) {
  pendingVolume_ = volume > kMaxVolume ? kMaxVolume : volume;
  poll(millis());
}

void Mp3Player::play(uint8_t folder, uint8_t track) {
  if (folder == 0 || track == 0) return;
  pendingPlay_ = static_cast<uint16_t>((folder << 8) | track);
  poll(millis());
}

void Mp3Player::stop() {
  pendingPlay_ = 0;
  if (canSend(millis())) sendCommand(kCmdStop, 0);
}

void Mp3Player::poll(uint32_t nowMs) {
  // One command per gap. Play goes first: it is what the player hears, and a
  // volume change can wait one gap.
  if (!canSend(nowMs)) return;
  if (pendingPlay_ != 0) {
    sendCommand(kCmdPlayFolderTrack, pendingPlay_);
    pendingPlay_ = 0;
  } else if (pendingVolume_ >= 0) {
    sendCommand(kCmdVolume, static_cast<uint16_t>(pendingVolume_));
    pendingVolume_ = -1;
  }
}

void Mp3Player::sendCommand(uint8_t cmd, uint16_t param) {
  // 7E FF 06 CMD 00 P_HI P_LO CK_HI CK_LO EF, checksum = -(sum of bytes 1..6)
  uint8_t frame[10] = {0x7E, 0xFF, 0x06, cmd, 0x00, static_cast<uint8_t>(param >> 8),
                       static_cast<uint8_t>(param & 0xFF), 0, 0, 0xEF};
  uint16_t sum = 0;
  for (uint8_t i = 1; i <= 6; ++i) sum += frame[i];
  const uint16_t checksum = static_cast<uint16_t>(0 - sum);
  frame[7] = static_cast<uint8_t>(checksum >> 8);
  frame[8] = static_cast<uint8_t>(checksum & 0xFF);
  serial_->write(frame, sizeof(frame));
  sentAny_ = true;
  lastSendMs_ = millis();
}

}  // namespace tr::io
