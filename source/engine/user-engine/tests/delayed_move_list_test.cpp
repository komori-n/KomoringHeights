#include <gtest/gtest.h>

#include "../delayed_move_list.hpp"
#include "test_lib.hpp"

using komori::DelayedMoveList;

TEST(DelayedMoveListTest, OrDrop) {
  TestNode n{"l8/4k4/2pnp2p1/p2p1pp1p/5P1rn/5n2P/Pp1LP1G2/8K/L2+r4L w 2b3g4sn5p 1", true};
  DelayedMoveList delayed_move_list{*n, n.MovePicker()};

  int next_i = 0;
  for (const auto& move : n.MovePicker()) {
    const auto i = next_i++;
    if (is_drop(move.move)) {
      EXPECT_FALSE(delayed_move_list.Prev(i)) << i << " " << move.move;
      EXPECT_FALSE(delayed_move_list.Next(i)) << i << " " << move.move;
    }
  }
}

TEST(DelayedMoveListTest, NoPromote) {
  TestNode n{"4k4/9/4P4/9/9/9/9/9/9 b 2r2b4g4s4n4l17p 1", true};
  DelayedMoveList delayed_move_list{*n, n.MovePicker()};
  int next_i = 0;
  int no_promote_i = 0;
  int promote_i = 0;
  for (const auto& move : n.MovePicker()) {
    const auto i = next_i++;
    if (move.move == make_move(SQ_53, SQ_52, B_PAWN)) {
      EXPECT_EQ(*delayed_move_list.Prev(i), promote_i) << i << " " << move.move;
      EXPECT_FALSE(delayed_move_list.Next(i)) << i << " " << move.move;
      EXPECT_EQ(no_promote_i, i);
    } else if (move.move == make_move_promote(SQ_53, SQ_52, B_PAWN)) {
      promote_i = i;
      EXPECT_FALSE(delayed_move_list.Prev(i)) << i << " " << move.move;
      EXPECT_TRUE(delayed_move_list.Next(i)) << i << " " << move.move;
      no_promote_i = *delayed_move_list.Next(i);
    }
  }
}

TEST(DelayedMoveListTest, LancePromote) {
  TestNode n{"4k4/4p4/4L4/9/9/9/9/9/9 b 2r2b4g4s4n3l17p 1", true};
  DelayedMoveList delayed_move_list{*n, n.MovePicker()};
  int next_i = 0;
  int no_promote_i = 0;
  int promote_i = 0;
  for (const auto& move : n.MovePicker()) {
    const auto i = next_i++;
    if (move.move == make_move(SQ_53, SQ_52, B_LANCE)) {
      EXPECT_EQ(*delayed_move_list.Prev(i), promote_i) << i << " " << move.move;
      EXPECT_FALSE(delayed_move_list.Next(i)) << i << " " << move.move;
      EXPECT_EQ(no_promote_i, i);
    } else if (move.move == make_move_promote(SQ_53, SQ_52, B_LANCE)) {
      promote_i = i;
      EXPECT_FALSE(delayed_move_list.Prev(i)) << i << " " << move.move;
      EXPECT_TRUE(delayed_move_list.Next(i)) << i << " " << move.move;
      no_promote_i = *delayed_move_list.Next(i);
    }
  }
}

TEST(DelayedMoveListTest, AndDrop) {
  TestNode n{"9/9/9/9/9/9/9/R7k/9 w r2b4g4s4n4l18p 1", false};
  DelayedMoveList delayed_move_list{*n, n.MovePicker()};
  int next_i = 0;
  int no_prev_cnt = 0;
  int no_next_cnt = 0;
  for (const auto& move : n.MovePicker()) {
    const auto i = next_i++;
    if (is_drop(move.move)) {
      if (!delayed_move_list.Prev(i)) {
        no_prev_cnt++;
      }
      if (!delayed_move_list.Next(i)) {
        no_next_cnt++;
      }
    }
  }

  EXPECT_EQ(no_prev_cnt, 7);
  EXPECT_EQ(no_next_cnt, 7);
}

TEST(DelayedMoveListTest, LancePromoteRev) {
  TestNode n{"9/3l5/9/9/9/9/9/3R1k3/9 w r2b4g4s4n3l18p 1", false};
  DelayedMoveList delayed_move_list{*n, n.MovePicker()};
  int next_i = 0;
  int no_promote_i = 0;
  int promote_i = 0;
  for (const auto& move : n.MovePicker()) {
    const auto i = next_i++;
    if (move.move == make_move(SQ_62, SQ_68, W_LANCE)) {
      EXPECT_EQ(*delayed_move_list.Prev(i), promote_i) << i << " " << move.move;
      EXPECT_FALSE(delayed_move_list.Next(i)) << i << " " << move.move;
      EXPECT_EQ(no_promote_i, i);
    } else if (move.move == make_move_promote(SQ_62, SQ_68, W_LANCE)) {
      promote_i = i;
      EXPECT_FALSE(delayed_move_list.Prev(i)) << i << " " << move.move;
      EXPECT_TRUE(delayed_move_list.Next(i)) << i << " " << move.move;
      no_promote_i = *delayed_move_list.Next(i);
    }
  }
}
