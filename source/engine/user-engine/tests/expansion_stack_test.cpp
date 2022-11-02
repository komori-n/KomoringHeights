#include <gtest/gtest.h>

#include "../expansion_stack.hpp"
#include "test_lib.hpp"

using komori::ExpansionStack;
using komori::kDepthMaxMateLen;
using komori::tt::TranspositionTable;

TEST(ExpansionStackTest, Emplace) {
  TestNode n("4k4/9/9/9/9/9/9/9/9 b P2r2b4g4s4n4l17p 1", true);
  TranspositionTable tt;
  tt.Resize(1);
  ExpansionStack expansion_list;

  auto& expansion = expansion_list.Emplace(tt, *n, kDepthMaxMateLen, false);
  EXPECT_EQ(&expansion, &expansion_list.Current());
}

TEST(ExpansionStackTest, Pop) {
  TestNode n("4k4/9/9/9/9/9/9/9/9 b P2r2b4g4s4n4l17p 1", true);
  TranspositionTable tt;
  tt.Resize(1);
  ExpansionStack expansion_list;

  auto& e1 = expansion_list.Emplace(tt, *n, kDepthMaxMateLen, false);

  n->DoMove(make_move_drop(PAWN, SQ_52, BLACK));
  auto& e2 = expansion_list.Emplace(tt, *n, kDepthMaxMateLen, false);
  EXPECT_EQ(&e2, &expansion_list.Current());

  expansion_list.Pop();
  EXPECT_EQ(&e1, &expansion_list.Current());
}

TEST(ExpansionStackTest, Current) {
  TestNode n("4k4/9/9/9/9/9/9/9/9 b P2r2b4g4s4n4l17p 1", true);
  TranspositionTable tt;
  tt.Resize(1);
  ExpansionStack expansion_list;

  auto& expansion = expansion_list.Emplace(tt, *n, kDepthMaxMateLen, false);
  EXPECT_EQ(&expansion, &expansion_list.Current());
  EXPECT_EQ(&expansion, &const_cast<const ExpansionStack&>(expansion_list).Current());
}
