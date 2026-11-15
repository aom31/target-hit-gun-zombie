// Hub firmware: Wi-Fi access point, phone web UI and the game engine.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "app.h"
#include "hub_settings.h"
#include "storage.h"

using namespace tr;

namespace {

constexpr uint8_t kMaxPhones = 6;
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApMask(255, 255, 255, 0);

hub::App app;

// Strongest access point named `ssid`; returns its channel, or -1 if none is in range.
int8_t findChannel(const String& ssid) {
  const int16_t n = WiFi.scanNetworks(false, false, false, 300, 0, ssid.c_str());
  int8_t channel = -1;
  int32_t best = -1000;
  for (int16_t i = 0; i < n; ++i) {
    if (WiFi.SSID(i) == ssid && WiFi.RSSI(i) > best) {
      best = WiFi.RSSI(i);
      channel = static_cast<int8_t>(WiFi.channel(i));
    }
  }
  WiFi.scanDelete();
  if (channel > 0) Serial.printf("# shop Wi-Fi \"%s\" on channel %d (%ld dBm)\n", ssid.c_str(), channel, (long)best);
  return channel;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("# Target Range hub, firmware %s\n", kFirmwareVersion);

  const bool storageOk = hub::storage::begin();
  if (!storageOk) Serial.println("# data partition mount failed: settings and history won't be saved");
  if (!hub::storage::webReady()) Serial.println("# web UI missing: run `pio run -e hub -t uploadfs`");

  hub::HubSettings settings;
  if (!settings.load() && storageOk) {
    Serial.println("# no settings yet, writing defaults");
    settings.save();
  }

  bool uplink = settings.cloud.enabled && settings.cloud.ssid.length() > 0;
  if (uplink) {
    // The ESP32 has one radio: the hub's own Wi-Fi and ESP-NOW must sit on the shop
    // router's channel. Find it once now, then join with that channel fixed so the
    // radio never scans (hops) during a game. If the router moves channel, reboot the hub.
    WiFi.mode(WIFI_AP_STA);
    const int8_t ch = findChannel(settings.cloud.ssid);
    if (ch > 0) {
      settings.wifi.channel = static_cast<uint8_t>(ch);  // runtime only, not saved
    } else {
      // Don't try to join a router that isn't there: every attempt scans all channels,
      // which takes the hub's own Wi-Fi off the air and drops every phone.
      Serial.printf("# shop Wi-Fi \"%s\" not found (2.4 GHz, in range?), cloud off until reboot\n",
                    settings.cloud.ssid.c_str());
      settings.cloud.routerFound = false;
      uplink = false;
      WiFi.mode(WIFI_AP);
    }
  } else {
    WiFi.mode(WIFI_AP);
  }
  WiFi.softAPConfig(kApIp, kApIp, kApMask);
  if (!WiFi.softAP(settings.wifi.ssid.c_str(), settings.wifi.password.c_str(), settings.wifi.channel, 0, kMaxPhones)) {
    Serial.println("# soft AP failed to start");
  }
  esp_wifi_set_ps(WIFI_PS_NONE);
  Serial.printf("# Wi-Fi \"%s\" on channel %u, open http://%s/\n", settings.wifi.ssid.c_str(), settings.wifi.channel,
                WiFi.softAPIP().toString().c_str());
  if (uplink) {
    // No automatic reconnect: a retry against a missing router scans every channel and
    // drops the phones. CloudLink retries itself, rarely and never during a game.
    WiFi.setAutoReconnect(false);
    WiFi.begin(settings.cloud.ssid.c_str(), settings.cloud.password.c_str(), settings.wifi.channel);
    Serial.printf("# joining shop Wi-Fi \"%s\" for the cloud live view\n", settings.cloud.ssid.c_str());
  }

  if (!app.begin(settings, storageOk)) Serial.println("# app failed to start");
}

void loop() {
  // All work happens on the app task and the async web server tasks.
  vTaskDelete(nullptr);
}
