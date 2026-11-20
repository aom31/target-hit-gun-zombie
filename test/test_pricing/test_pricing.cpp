#include <unity.h>

#include "pricing.h"

using namespace tr::game;

namespace {

PriceList menu() {
  PriceList list;
  list.enabled = true;
  list.durationsS = {30, 60, 90};
  list.trialDurationS = 20;
  list.trialPrice = 50;
  list.guns = {
      {"AR GBB", GunKind::Long, "ต้องเปลี่ยนแม็ก", {100, 150, 200}},
      {"AR AEG", GunKind::Electric, "", {80, 120, 160}},
  };
  return list;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_price_comes_from_the_gun_and_time_cell() {
  QuoteRequest r;
  r.durationS = 60;
  r.gun[0] = 1;
  const Quote q = quote(menu(), r);
  TEST_ASSERT_EQUAL(static_cast<int>(QuoteError::None), static_cast<int>(q.error));
  TEST_ASSERT_EQUAL_UINT16(60, q.durationS);
  TEST_ASSERT_EQUAL_UINT32(120, q.total);
}

void test_duel_adds_each_players_gun() {
  QuoteRequest r;
  r.durationS = 90;
  r.players = 2;
  r.gun = {0, 1};
  const Quote q = quote(menu(), r);
  TEST_ASSERT_EQUAL_UINT32(200, q.price[0]);
  TEST_ASSERT_EQUAL_UINT32(160, q.price[1]);
  TEST_ASSERT_EQUAL_UINT32(360, q.total);
}

void test_trial_is_one_price_and_its_own_time() {
  QuoteRequest r;
  r.trial = true;
  r.durationS = 90;  // ignored
  r.gun[0] = 0;
  const Quote q = quote(menu(), r);
  TEST_ASSERT_EQUAL_UINT16(20, q.durationS);
  TEST_ASSERT_EQUAL_UINT32(50, q.total);
}

void test_rejects_unknown_time_and_missing_gun() {
  QuoteRequest r;
  r.durationS = 45;
  r.gun[0] = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(QuoteError::BadDuration), static_cast<int>(quote(menu(), r).error));

  r.durationS = 30;
  r.gun[0] = -1;
  TEST_ASSERT_EQUAL(static_cast<int>(QuoteError::NoGun), static_cast<int>(quote(menu(), r).error));

  r.gun[0] = 7;
  TEST_ASSERT_EQUAL(static_cast<int>(QuoteError::BadGun), static_cast<int>(quote(menu(), r).error));
}

void test_no_guns_configured_is_free_and_needs_no_gun() {
  PriceList list;  // pricing off, no guns
  QuoteRequest r;
  r.durationS = 60;
  const Quote q = quote(list, r);
  TEST_ASSERT_EQUAL(static_cast<int>(QuoteError::None), static_cast<int>(q.error));
  TEST_ASSERT_EQUAL_UINT32(0, q.total);
}

void test_validation() {
  TEST_ASSERT_NULL(menu().validate());
  TEST_ASSERT_NULL(defaultPriceList().validate());

  PriceList p = menu();
  p.durationsS = {60, 30, 90};
  TEST_ASSERT_NOT_NULL(p.validate());  // not ascending

  p = menu();
  p.guns[0].prices = {100, 150};
  TEST_ASSERT_NOT_NULL(p.validate());  // a price missing

  p = menu();
  p.guns.clear();
  TEST_ASSERT_NOT_NULL(p.validate());  // pricing on but nothing to sell

  p = menu();
  p.guns[1].name = "";
  TEST_ASSERT_NOT_NULL(p.validate());

  p = menu();
  p.promptpayId = "0812345678";
  TEST_ASSERT_NULL(p.validate());
  p.promptpayId = "1234567890123";
  TEST_ASSERT_NULL(p.validate());
  p.promptpayId = "081-234-5678";
  TEST_ASSERT_NOT_NULL(p.validate());  // digits only
  p.promptpayId = "08123";
  TEST_ASSERT_NOT_NULL(p.validate());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_price_comes_from_the_gun_and_time_cell);
  RUN_TEST(test_duel_adds_each_players_gun);
  RUN_TEST(test_trial_is_one_price_and_its_own_time);
  RUN_TEST(test_rejects_unknown_time_and_missing_gun);
  RUN_TEST(test_no_guns_configured_is_free_and_needs_no_gun);
  RUN_TEST(test_validation);
  return UNITY_END();
}
