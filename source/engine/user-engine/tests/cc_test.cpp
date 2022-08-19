#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "../cc.hpp"
#include "../initial_estimation.hpp"
#include "test_lib.hpp"

using komori::ChildrenCache;
using komori::kInfinitePnDn;
using komori::MateLen;
using komori::detail::IndexTable;

TEST(IndexTableTest, Push) {
  IndexTable idx;

  EXPECT_EQ(idx.Push(2), 0);
  EXPECT_EQ(idx.Push(6), 1);
  EXPECT_EQ(idx.Push(4), 2);
}

TEST(IndexTableTest, Pop) {
  IndexTable idx;

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);

  EXPECT_EQ(idx.size(), 3);
  idx.Pop();
  EXPECT_EQ(idx.size(), 2);
}

TEST(IndexTableTest, operator) {
  IndexTable idx;

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);

  EXPECT_EQ(idx[0], 2);
  EXPECT_EQ(idx[1], 6);
  EXPECT_EQ(idx[2], 4);
}

TEST(IndexTableTest, iterators) {
  IndexTable idx;

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);
  EXPECT_EQ(*idx.begin(), 2);
  EXPECT_EQ(idx.begin() + 3, idx.end());
  EXPECT_EQ(*idx.begin(), idx.front());
}

TEST(IndexTableTest, size) {
  IndexTable idx;
  EXPECT_TRUE(idx.empty());
  EXPECT_EQ(idx.size(), 0);

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);
  EXPECT_FALSE(idx.empty());
  EXPECT_EQ(idx.size(), 3);
}

namespace {
class ChildrenCacheTest : public ::testing::Test {
 protected:
  void SetUp() override { tt_.Resize(1); }

  komori::tt::TranspositionTable tt_;
};
}  // namespace

TEST_F(ChildrenCacheTest, NoLegalMoves) {
  TestNode n{"4k4/9/9/9/9/9/9/9/9 b 2r2b4g4s4n4l18p 1", true};
  ChildrenCache cc{tt_, *n, MateLen::Make(33, 4), true};

  const auto res = cc.CurrentResult(*n);
  EXPECT_EQ(res.Pn(), kInfinitePnDn);
  EXPECT_EQ(res.Dn(), 0);
}

TEST_F(ChildrenCacheTest, ObviousNomate) {
  TestNode n{"lnsgkgsnl/1r2G2b1/ppppppppp/9/9/9/PPPPPPPPP/9/LNS1KGSNL w rb 1", false};
  ChildrenCache cc{tt_, *n, MateLen::Make(33, 4), true};

  const auto res = cc.CurrentResult(*n);
  EXPECT_EQ(res.Pn(), kInfinitePnDn);
  EXPECT_EQ(res.Dn(), 0);
}

TEST_F(ChildrenCacheTest, ObviousMate) {
  TestNode n{"7kG/7p1/9/7N1/9/9/9/9/9 w G2r2b2g4s3n4l17p 1", false};
  ChildrenCache cc{tt_, *n, MateLen::Make(33, 4), true};

  const auto res = cc.CurrentResult(*n);
  EXPECT_EQ(res.Pn(), 0);
  EXPECT_EQ(res.Dn(), kInfinitePnDn);
}

TEST_F(ChildrenCacheTest, DelayExpansion) {
  TestNode n{"6R1k/7lp/9/9/9/9/9/9/9 w r2b4g4s4n3l17p 1", false};
  ChildrenCache cc{tt_, *n, MateLen::Make(33, 4), true};

  const auto [pn, dn] = komori::InitialPnDn(*n, make_move_drop(ROOK, SQ_21, BLACK));
  const auto res = cc.CurrentResult(*n);
  EXPECT_EQ(res.Pn(), pn + 1);
  EXPECT_EQ(res.Dn(), dn);
}

TEST_F(ChildrenCacheTest, ObviousRepetition) {
  TestNode n{"7lk/7p1/9/8L/8p/9/9/9/9 w 2r2b4g4s4n2l16p 1", false};
  n->DoMove(make_move_drop(LANCE, SQ_13, WHITE));
  n->DoMove(make_move(SQ_14, SQ_13, B_LANCE));
  n->DoMove(make_move_drop(GOLD, SQ_12, WHITE));
  n->DoMove(make_move(SQ_13, SQ_12, B_LANCE));
  n->DoMove(make_move(SQ_11, SQ_12, W_KING));
  n->DoMove(make_move_drop(GOLD, SQ_11, BLACK));
  n->DoMove(make_move(SQ_12, SQ_11, W_KING));
  n->DoMove(make_move_drop(LANCE, SQ_15, BLACK));
  n->DoMove(make_move_drop(LANCE, SQ_13, WHITE));
  n->DoMove(make_move(SQ_15, SQ_13, B_LANCE));
  n->DoMove(make_move_drop(GOLD, SQ_12, WHITE));
  n->DoMove(make_move(SQ_13, SQ_12, B_LANCE));
  n->DoMove(make_move(SQ_11, SQ_12, W_KING));
  n->DoMove(make_move_drop(GOLD, SQ_11, BLACK));
  n->DoMove(make_move(SQ_12, SQ_11, W_KING));
  ChildrenCache cc{tt_, *n, MateLen::Make(33, 4), true};

  const auto res = cc.CurrentResult(*n);
  EXPECT_EQ(res.Pn(), kInfinitePnDn);
  EXPECT_EQ(res.Dn(), 0);
}

TEST_F(ChildrenCacheTest, InitialSort) {
  TestNode n{"7k1/6pP1/7LP/8L/9/9/9/9/9 w 2r2b4g4s4n2l15p 1", false};
  ChildrenCache cc{tt_, *n, MateLen::Make(33, 4), true};

  const auto [pn, dn] = komori::InitialPnDn(*n, make_move(SQ_21, SQ_31, W_KING));
  const auto res = cc.CurrentResult(*n);
  EXPECT_EQ(res.Pn(), pn);
  EXPECT_EQ(res.Dn(), dn);
}

TEST_F(ChildrenCacheTest, MaxChildren) {
  TestNode n{"6pkp/7PR/7L1/9/9/9/9/9/9 w r2b4g4s4n3l15p 1", false};
  ChildrenCache cc{tt_, *n, MateLen::Make(33, 4), true, komori::BitSet64{}};

  const auto [pn1, dn1] = komori::InitialPnDn(*n, make_move(SQ_21, SQ_12, W_KING));
  const auto [pn2, dn2] = komori::InitialPnDn(*n, make_move(SQ_21, SQ_32, W_KING));
  const auto res = cc.CurrentResult(*n);
  EXPECT_EQ(res.Pn(), std::max(pn1, pn2));
  EXPECT_EQ(res.Dn(), std::min(dn1, dn2));
}
