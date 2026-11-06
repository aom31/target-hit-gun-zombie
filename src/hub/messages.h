#pragma once

// JSON messages sent from the hub to the phones (WebSocket protocol, hub -> client).
// The client side lives in web/app.js; keep both in sync.

#include <Arduino.h>
#include <ArduinoJson.h>

#include "cloud_link.h"
#include "crosstalk_test.h"
#include "game_types.h"
#include "hub_settings.h"
#include "node_registry.h"
#include "sd_archive.h"
#include "storage.h"

namespace tr::hub::msg {

String hello(const HubSettings& s, bool admin);
String publicConfig(const HubSettings& s);
String state(const game::GameSnapshot& g, uint32_t nowMs, const NodeRegistry& nodes, const char* busy);
String event(const game::GameEvent& e);
void result(JsonObject out, const game::GameResult& r, uint32_t id, uint64_t epochMs);
String resultMessage(const game::GameResult& r, uint32_t id, uint64_t epochMs);
String nodesDetail(const NodeRegistry& nodes, const HubSettings& s);
String peaks(uint8_t nodeId, uint8_t zone, const uint16_t* peaks, bool fromHit);
String crosstalk(const CrosstalkTest& t, const CrosstalkReport* report);
String settings(const HubSettings& s);
String storageStatus(const tr::hub::storage::Usage& flash, uint16_t games, uint16_t maxGames, const SdArchive::Status& sd);
String authResult(bool ok);
String cloudStatus(const CloudLink::Status& s);
String error(const char* code, const String& message);
String ok(const char* what, const String& message = String());

}  // namespace tr::hub::msg
