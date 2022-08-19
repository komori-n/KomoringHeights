#include <gtest/gtest.h>

#include "../../thread.h"
#include "test_lib.hpp"

using komori::MovePicker;

TEST(MovePickerTest, OrNode_Normal) {
  TestNode n{"4k4/9/9/9/9/9/9/9/9 b P2r2b4g4s4n4l17p 1", true};
  const auto& mp = n.MovePicker();

  EXPECT_EQ(mp.size(), 1);
  EXPECT_FALSE(mp.empty());
  const auto move = mp[0];
  EXPECT_EQ(move, make_move_drop(PAWN, SQ_52, BLACK));
}

TEST(MovePickerTest, OrNode_InCheck) {
  TestNode n{"4k4/3s5/3PK4/9/9/9/9/9/9 b P2r2b4g3s4n4l16p 1", true};
  const auto& mp = n.MovePicker();

  EXPECT_EQ(mp.size(), 1);
  EXPECT_FALSE(mp.empty());
  const auto move = mp[0];
  EXPECT_EQ(move, make_move_promote(SQ_63, SQ_62, B_PAWN));
}

TEST(MovePickerTest, OrNode_Empty) {
  TestNode n{"lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1", true};
  const auto& mp = n.MovePicker();

  EXPECT_EQ(mp.size(), 0);
  EXPECT_TRUE(mp.empty());
}

TEST(MovePickerTest, AndNode) {
  TestNode n{"4k4/4+P4/9/9/9/9/9/9/9 w P2r2b4g4s4n4l16p 1", false};
  const auto& mp = n.MovePicker();

  EXPECT_EQ(mp.size(), 1);
  EXPECT_FALSE(mp.empty());
  const auto move = mp[0];
  EXPECT_EQ(move, make_move(SQ_51, SQ_52, W_KING));
}
