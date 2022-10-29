#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "../initial_estimation.hpp"
#include "../local_expansion.hpp"
#include "test_lib.hpp"

using komori::kInfinitePnDn;
using komori::LocalExpansion;
using komori::MateLen;
using komori::detail::CheckObviousFinalOrNode;

namespace {
TEST(CheckObviousFinalOrNode, Ambiguous) {
  const std::vector<std::string> tests{
      "k8/9/9/9/9/9/9/9/9 b P2r2b4g4s4n4l17p 1",
      "k8/9/9/9/9/9/9/9/9 b G2r2b3g4s4n4l18p 1",
      "k8/9/G8/9/9/9/9/9/9 b 2r2b3g4s4n4l18p 1",
  };

  for (const auto& s : tests) {
    TestNode n{s, true};
    EXPECT_FALSE(CheckObviousFinalOrNode(*n)) << s;
  }
}

TEST(CheckObviousFinalOrNode, MateIn1Ply) {
  const std::vector<std::pair<std::string, Hand>> tests{
      {"k8/9/P8/9/9/9/9/9/9 b G2r2b3g4s4n4l17p 1", MakeHand<GOLD>()},
      {"k8/9/P8/9/9/9/9/9/9 b 2R2B4G4S4N4LP16p 1", MakeHand<GOLD>()},
      {"kp7/9/GG7/2b6/9/9/9/9/9 b 2rb2g4s4n4l17p 1", MakeHand<>()},
  };

  for (const auto& [s, h] : tests) {
    TestNode n{s, true};

    auto res = CheckObviousFinalOrNode(*n);
    ASSERT_TRUE(res) << s;
    EXPECT_TRUE(res->IsFinal()) << s;
    EXPECT_EQ(res->Pn(), 0) << s;
    EXPECT_EQ(res->GetHand(), h) << s;
  }
}

TEST(CheckObviousFinalOrNode, NoMate) {
  const std::vector<std::string> tests{
      "4k4/9/9/9/9/9/9/9/9 b 2r2b4g4s4n4l18p 1",
      "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/9/LNSGKGSNL b rb 1",
      "4k4/9/9/PPPPPPPPP/9/9/9/9/9 b 2r2b4g4s4n4l9p 1",
  };

  for (const auto& s : tests) {
    TestNode n{s, true};

    auto res = CheckObviousFinalOrNode(*n);
    ASSERT_TRUE(res) << s;
    EXPECT_TRUE(res->IsFinal()) << s;
    EXPECT_EQ(res->Dn(), 0) << s;
  }
}

class LocalExpansionTest : public ::testing::Test {
 protected:
  void SetUp() override { tt_.Resize(1); }

  komori::ttv3::TranspositionTable tt_;
};
}  // namespace

TEST_F(LocalExpansionTest, NoLegalMoves) {
  TestNode n{"4k4/9/9/9/9/9/9/9/9 b 2r2b4g4s4n4l18p 1", true};
  LocalExpansion local_expansion{tt_, *n, MateLen::Make(33, 4), true};

  const auto res = local_expansion.CurrentResult(*n);
  EXPECT_EQ(res.Pn(), kInfinitePnDn);
  EXPECT_EQ(res.Dn(), 0);
}

TEST_F(LocalExpansionTest, DelayExpansion) {
  TestNode n{"6R1k/7lp/9/9/9/9/9/9/9 w r2b4g4s4n3l17p 1", false};
  LocalExpansion local_expansion{tt_, *n, MateLen::Make(33, 4), true};

  const auto [pn, dn] = komori::InitialPnDn(*n, make_move_drop(ROOK, SQ_21, BLACK));
  const auto res = local_expansion.CurrentResult(*n);
  EXPECT_EQ(res.Pn(), pn + 1);
  EXPECT_EQ(res.Dn(), dn);
}

TEST_F(LocalExpansionTest, ObviousRepetition) {
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
  LocalExpansion local_expansion{tt_, *n, MateLen::Make(33, 4), true};

  const auto res = local_expansion.CurrentResult(*n);
  EXPECT_EQ(res.Pn(), kInfinitePnDn);
  EXPECT_EQ(res.Dn(), 0);
}

TEST_F(LocalExpansionTest, InitialSort) {
  TestNode n{"7k1/6pP1/7LP/8L/9/9/9/9/9 w 2r2b4g4s4n2l15p 1", false};
  LocalExpansion local_expansion{tt_, *n, MateLen::Make(33, 4), true};

  const auto [pn, dn] = komori::InitialPnDn(*n, make_move(SQ_21, SQ_31, W_KING));
  const auto res = local_expansion.CurrentResult(*n);
  EXPECT_EQ(res.Pn(), pn);
  EXPECT_EQ(res.Dn(), dn);
}

TEST_F(LocalExpansionTest, MaxChildren) {
  TestNode n{"6pkp/7PR/7L1/9/9/9/9/9/9 w r2b4g4s4n3l15p 1", false};
  LocalExpansion local_expansion{tt_, *n, MateLen::Make(33, 4), true, komori::BitSet64{}};

  const auto [pn1, dn1] = komori::InitialPnDn(*n, make_move(SQ_21, SQ_12, W_KING));
  const auto [pn2, dn2] = komori::InitialPnDn(*n, make_move(SQ_21, SQ_32, W_KING));
  const auto res = local_expansion.CurrentResult(*n);
  EXPECT_EQ(res.Pn(), std::max(pn1, pn2));
  EXPECT_EQ(res.Dn(), std::min(dn1, dn2));
}
