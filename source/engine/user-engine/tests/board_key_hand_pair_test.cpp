#include <gtest/gtest.h>

#include "../board_key_hand_pair.hpp"
#include "test_lib.hpp"

using komori::BoardKeyHandPair;

TEST(BoardKeyHandPair, OperatorEqual) {
  BoardKeyHandPair p1{0x334, MakeHand<PAWN, LANCE, LANCE>()};
  BoardKeyHandPair p2{0x334, MakeHand<PAWN>()};
  BoardKeyHandPair p3{0x264, MakeHand<PAWN, LANCE, LANCE>()};

  EXPECT_EQ(p1, p1);
  EXPECT_NE(p1, p2);
  EXPECT_NE(p1, p3);
}
