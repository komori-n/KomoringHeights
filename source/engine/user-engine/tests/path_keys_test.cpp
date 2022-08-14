#include <gtest/gtest.h>

#include "../path_keys.hpp"

namespace {
class PathKeysTest : public ::testing::Test {
 protected:
  void SetUp() override { komori::PathKeyInit(); }
};
}  // namespace

TEST_F(PathKeysTest, PathKeyAfter_drop) {
  const Key before_key = 0x334334;
  const Key after_key = komori::PathKeyAfter(before_key, make_move_drop(PAWN, SQ_88, BLACK), 264);

  Key expected_key = before_key;

  expected_key ^= komori::detail::g_move_to[SQ_88][264];
  expected_key ^= komori::detail::g_dropped_pr[PAWN][264];

  EXPECT_EQ(after_key, expected_key);
}