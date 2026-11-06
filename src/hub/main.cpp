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

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(kApIp, kApIp, kApMask);
  if (!WiFi.softAP(settings.wifi.ssid.c_str(), settings.wifi.password.c_str(), settings.wifi.channel, 0, kMaxPhones)) {
    Serial.println("# soft AP failed to start");
  }
  esp_wifi_set_ps(WIFI_PS_NONE);
  Serial.printf("# Wi-Fi \"%s\" on channel %u, open http://%s/\n", settings.wifi.ssid.c_str(), settings.wifi.channel,
                WiFi.softAPIP().toString().c_str());

  if (!app.begin(settings, storageOk)) Serial.println("# app failed to start");
}

void loop() {
  // All work happens on the app task and the async web server tasks.
  vTaskDelete(nullptr);
}
