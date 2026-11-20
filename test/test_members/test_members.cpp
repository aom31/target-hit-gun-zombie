#include <unity.h>

#include "members.h"

using namespace tr::game;

namespace {

// Stand-in for SHA-256: enough to tell salts and PINs apart.
std::string fakeHash(const std::string& salt, const std::string& pin) { return salt + "|" + pin; }

uint32_t addOk(MemberBook& book, const char* name, const char* pin, const char* salt = "s1") {
  uint32_t id = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(MemberError::None),
                    static_cast<int>(book.add(name, pin, "", salt, "0123456789abcdef0123456789abcdef", 0, id)));
  return id;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_name_key_ignores_spaces_and_latin_case() {
  TEST_ASSERT_EQUAL_STRING("ต้น", nameKey("  ต้น  ").c_str());
  TEST_ASSERT_EQUAL_STRING("big boss", nameKey("Big   BOSS ").c_str());
  TEST_ASSERT_EQUAL_STRING("Big BOSS", cleanMemberName(" Big   BOSS ").c_str());
}

void test_register_rejects_taken_names_and_bad_input() {
  MemberBook book(fakeHash);
  addOk(book, "ต้น", "1234");
  uint32_t id = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(MemberError::NameTaken), static_cast<int>(book.add(" ต้น ", "5555", "", "s", "k", 0, id)));
  TEST_ASSERT_EQUAL(static_cast<int>(MemberError::BadPin), static_cast<int>(book.add("ฟ้า", "12a4", "", "s", "k", 0, id)));
  TEST_ASSERT_EQUAL(static_cast<int>(MemberError::BadPin), static_cast<int>(book.add("ฟ้า", "123", "", "s", "k", 0, id)));
  TEST_ASSERT_EQUAL(static_cast<int>(MemberError::BadLast4), static_cast<int>(book.add("ฟ้า", "1234", "12", "s", "k", 0, id)));
  TEST_ASSERT_EQUAL(static_cast<int>(MemberError::BadName), static_cast<int>(book.add("   ", "1234", "", "s", "k", 0, id)));
  TEST_ASSERT_EQUAL(static_cast<int>(MemberError::None), static_cast<int>(book.add("ฟ้า", "1234", "4567", "s", "k", 0, id)));
  TEST_ASSERT_EQUAL_STRING("4567", book.find(id)->last4.c_str());
}

void test_pin_check_and_reset_bumps_version() {
  MemberBook book(fakeHash);
  const uint32_t id = addOk(book, "ต้น", "1234");
  const Member* m = book.findByName("ต้น");
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_TRUE(book.checkPin(*m, "1234"));
  TEST_ASSERT_FALSE(book.checkPin(*m, "4321"));
  const uint32_t before = m->pinVer;
  TEST_ASSERT_EQUAL(static_cast<int>(MemberError::None), static_cast<int>(book.resetPin(id, "9999", "s2")));
  m = book.find(id);
  TEST_ASSERT_EQUAL_UINT32(before + 1, m->pinVer);
  TEST_ASSERT_FALSE(book.checkPin(*m, "1234"));
  TEST_ASSERT_TRUE(book.checkPin(*m, "9999"));
}

void test_ids_are_never_reused_after_load_and_remove() {
  MemberBook book(fakeHash);
  addOk(book, "a", "1111");
  const uint32_t b = addOk(book, "b", "2222");
  TEST_ASSERT_TRUE(book.remove(b));
  TEST_ASSERT_NULL(book.findByName("b"));
  MemberBook reloaded(fakeHash);
  std::vector<Member> saved = book.all();
  Member gone;
  gone.id = 7;
  gone.name = "old";
  saved.push_back(gone);
  reloaded.load(saved);
  TEST_ASSERT_EQUAL_UINT32(8, reloaded.nextId());
}

void test_search_matches_part_of_the_name() {
  MemberBook book(fakeHash);
  addOk(book, "Ton Kung", "1111");
  addOk(book, "ฟ้า", "2222");
  TEST_ASSERT_EQUAL(1, static_cast<int>(book.search("ton", 10).size()));
  TEST_ASSERT_EQUAL(2, static_cast<int>(book.search("", 10).size()));
}

void test_login_limiter_locks_after_five_fails_then_expires() {
  LoginLimiter lim;
  for (int i = 0; i < kMaxLoginFails - 1; ++i) lim.fail("ต้น", 1000);
  TEST_ASSERT_FALSE(lim.locked("ต้น", 1000));
  lim.fail("ต้น", 1000);
  TEST_ASSERT_TRUE(lim.locked("ต้น", 2000));
  TEST_ASSERT_FALSE(lim.locked("ฟ้า", 2000));  // other names unaffected
  TEST_ASSERT_FALSE(lim.locked("ต้น", 1000 + kLoginLockMs + 1));
  lim.succeed("ต้น");
  TEST_ASSERT_FALSE(lim.locked("ต้น", 2000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_name_key_ignores_spaces_and_latin_case);
  RUN_TEST(test_register_rejects_taken_names_and_bad_input);
  RUN_TEST(test_pin_check_and_reset_bumps_version);
  RUN_TEST(test_ids_are_never_reused_after_load_and_remove);
  RUN_TEST(test_search_matches_part_of_the_name);
  RUN_TEST(test_login_limiter_locks_after_five_fails_then_expires);
  return UNITY_END();
}
