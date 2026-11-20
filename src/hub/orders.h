#pragma once

// Paid games waiting to be played.
//
// With pricing on, a customer's phone does not start a game directly: it places an
// order (the hub prices it), pays by PromptPay QR, staff confirm the money on any
// phone signed in as admin (or with the PIN), and only then can the customer press
// START. The order freezes mode, players, guns and play time, so nothing can change
// between paying and playing. Orders live in RAM: a hub restart drops them, and
// staff can still start a game directly from a signed-in phone.

#include <cstdint>

#include "game_types.h"

namespace tr::hub {

struct Order {
  uint32_t id = 0;
  uint32_t token = 0;  // known only to the phone that placed it (kept across a page reload)
  bool paid = false;
  uint32_t createdMs = 0;
  game::StartRequest req;  // what was paid for
};

constexpr uint8_t kMaxOrders = 6;
constexpr uint32_t kUnpaidOrderTtlMs = 15UL * 60UL * 1000UL;  // paid orders never expire

}  // namespace tr::hub
