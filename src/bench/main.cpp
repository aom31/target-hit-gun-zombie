// Bench-test firmware for one mannequin node, no radio and no hub required.
//
// Prints every detection with all five channel peaks as a JSON line (the old
// Flask dashboard in dashboard_new_v2/ can display them) and accepts single-key
// commands over USB serial at 115200 baud:
//
//   l  cycle each zone through red / green / blue (checks wiring and colour order)
//   o  cycle colour order RGB -> GRB -> BRG -> ...     p  toggle 800/400 kHz pixel timing
//   c  recalibrate the baseline (keep the mannequin still)
//   t  toggle tuning mode (trigger just above the noise floor)
//   s  print baseline / noise / trigger per channel
//   m  play /01/001.mp3 on the DFPlayer (hit sound)     + / -  volume up / down
//   1..9  set every channel's threshold to 100..900

#include <Arduino.h>
#include <esp_timer.h>

#include "mp3_player.h"
#include "node_pins.h"
#include "sensor_task.h"
#include "zone_leds.h"

using namespace tr;

namespace {

io::ZoneLeds leds;
io::SensorTask sensor;
io::Mp3Player player;
uint8_t volume = kDefaultVolume;
sensing::ArbiterConfig config;
ColorOrder colorOrder = ColorOrder::RGB;
LedProtocol protocol = LedProtocol::Khz800;
bool tuning = false;

const char* orderName(ColorOrder o) {
  static const char* names[] = {"RGB", "GRB", "BRG", "RBG", "GBR", "BGR"};
  return names[static_cast<uint8_t>(o) % 6];
}

void flashZone(uint8_t zone, LedColor color) {
  leds.apply(static_cast<uint8_t>(1u << zone), LedSteady{}, color, 250, millis());
}

void ledCycle() {
  Serial.printf("# LED test, colour order %s, %s\n", orderName(colorOrder),
                protocol == LedProtocol::Khz800 ? "800 kHz" : "400 kHz");
  const LedColor colors[] = {LedColor::Red, LedColor::Green, LedColor::Blue};
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    for (LedColor c : colors) {
      Serial.printf("#   Z%u %s\n", z + 1, c == LedColor::Red ? "red" : c == LedColor::Green ? "green" : "blue");
      leds.apply(static_cast<uint8_t>(1u << z), LedSteady{c, LedFx::Solid, 0}, LedColor::Off, 0, millis());
      const uint32_t until = millis() + 600;
      while (millis() < until) {
        leds.render(millis());
        delay(10);
      }
    }
    leds.allOff(millis());
  }
}

void printStats() {
  const io::SensorStats s = sensor.stats();
  Serial.printf("# calibrated=%d tuning=%d detections=%lu\n", s.calibrated, s.tuning,
                static_cast<unsigned long>(s.detections));
  for (uint8_t z = 0; z < kZoneCount; ++z) {
    Serial.printf("#   Z%u baseline=%4u noise=%3u trigger=%4u\n", z + 1, s.baseline[z], s.noise[z], s.trigger[z]);
  }
}

void handleKey(char key) {
  switch (key) {
    case 'l': ledCycle(); break;
    case 'o':
      colorOrder = static_cast<ColorOrder>((static_cast<uint8_t>(colorOrder) + 1) %
                                           static_cast<uint8_t>(ColorOrder::Count));
      leds.configure(255, colorOrder, protocol);
      ledCycle();
      break;
    case 'p':
      protocol = protocol == LedProtocol::Khz800 ? LedProtocol::Khz400 : LedProtocol::Khz800;
      leds.configure(255, colorOrder, protocol);
      ledCycle();
      break;
    case 'c':
      Serial.println("# recalibrating, keep still");
      sensor.requestCalibration();
      break;
    case 't':
      tuning = !tuning;
      sensor.setTuning(tuning);
      Serial.printf("# tuning %s\n", tuning ? "on" : "off");
      break;
    case 's': printStats(); break;
    case 'm':
      Serial.printf("# play /01/001.mp3 at volume %u\n", volume);
      player.play(kHitSoundFolder, 1);
      break;
    case '+':
    case '-':
      volume = static_cast<uint8_t>(key == '+' ? min<int>(volume + 2, kMaxVolume) : max<int>(volume - 2, 0));
      player.setVolume(volume);
      Serial.printf("# volume %u/30\n", volume);
      break;
    default:
      if (key >= '1' && key <= '9') {
        config.threshold.fill(static_cast<uint16_t>((key - '0') * 100));
        sensor.configure(config);
        Serial.printf("# threshold = %u on all channels\n", config.threshold[0]);
      }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("# Target Range bench firmware");
  leds.begin(pins::kPixelData, pins::kPixelsPerZone, protocol);
  leds.configure(255, colorOrder, protocol);
  leds.onFrameSent([] { sensor.guardFor(3000); });
  sensor.begin(pins::kPiezo, config);
  player.begin(Serial2, pins::kMp3Rx, pins::kMp3Tx);
  player.setVolume(volume);
  delay(800);
  printStats();
  Serial.println("# keys: l=LED test o=colour order p=pixel speed c=calibrate t=tuning s=stats m=sound +/-=volume 1..9=threshold x100");
  ledCycle();
}

void loop() {
  while (Serial.available() > 0) handleKey(static_cast<char>(Serial.read()));

  sensing::Detection d;
  while (sensor.nextDetection(d)) {
    flashZone(d.zone, LedColor::Blue);
    Serial.printf(
        "{\"event\":\"HIT\",\"target_id\":1,\"zone_id\":%u,\"peak_delta\":%u,\"peaks\":[%u,%u,%u,%u,%u],"
        "\"suppressed\":%u,\"timestamp_ms\":%lu}\n",
        d.zone + 1, d.peaks[d.zone], d.peaks[0], d.peaks[1], d.peaks[2], d.peaks[3], d.peaks[4], d.suppressedMask,
        static_cast<unsigned long>(d.atUs / 1000));
  }
  player.poll(millis());
  leds.render(millis());
  delay(2);
}
