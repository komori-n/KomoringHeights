#include <gtest/gtest.h>

#include <memory>
#include <string>

#define USE_DFPN_PLUS
#include "../initial_estimation.hpp"
#include "test_lib.hpp"

using komori::InitialPnDn;

TEST(InitialEstimationTest, InitialOrNode) {
  TestNode n{"2p1k1g2/1s3p1s1/4PP3/2R1L1R2/9/9/9/9/9 b L2b3g2s4n2l14p 1", true};

  EXPECT_EQ(InitialPnDn(*n, make_move_promote(SQ_43, SQ_42, B_PAWN)).first, 6);
  EXPECT_EQ(InitialPnDn(*n, make_move_promote(SQ_43, SQ_42, B_PAWN)).second, 2);
  EXPECT_EQ(InitialPnDn(*n, make_move(SQ_53, SQ_52, B_PAWN)).first, 4);
  EXPECT_EQ(InitialPnDn(*n, make_move(SQ_53, SQ_52, B_PAWN)).second, 2);
  EXPECT_EQ(InitialPnDn(*n, make_move_drop(LANCE, SQ_52, BLACK)).first, 2);
  EXPECT_EQ(InitialPnDn(*n, make_move_drop(LANCE, SQ_52, BLACK)).second, 4);
  EXPECT_EQ(InitialPnDn(*n, make_move(SQ_74, SQ_71, B_ROOK)).first, 4);
  EXPECT_EQ(InitialPnDn(*n, make_move(SQ_74, SQ_71, B_ROOK)).second, 2);
  EXPECT_EQ(InitialPnDn(*n, make_move(SQ_34, SQ_31, B_ROOK)).first, 2);
  EXPECT_EQ(InitialPnDn(*n, make_move(SQ_34, SQ_31, B_ROOK)).second, 4);
}

TEST(InitialEstimationTest, InitialAndNode) {
  TestNode n{"4k4/3s1s3/1r7/3NLN3/4g4/9/9/9/9 w r2b3g2s2n3l18p 1", false};

  EXPECT_EQ(InitialPnDn(*n, make_move(SQ_55, SQ_54, W_GOLD)).first, 4);
  EXPECT_EQ(InitialPnDn(*n, make_move(SQ_55, SQ_54, W_GOLD)).second, 2);
  EXPECT_EQ(InitialPnDn(*n, make_move(SQ_51, SQ_41, W_KING)).first, 2);
  EXPECT_EQ(InitialPnDn(*n, make_move(SQ_51, SQ_41, W_KING)).second, 2);
  EXPECT_EQ(InitialPnDn(*n, make_move(SQ_83, SQ_53, W_ROOK)).first, 4);
  EXPECT_EQ(InitialPnDn(*n, make_move(SQ_83, SQ_53, W_ROOK)).second, 2);
  EXPECT_EQ(InitialPnDn(*n, make_move_drop(PAWN, SQ_52, WHITE)).first, 2);
  EXPECT_EQ(InitialPnDn(*n, make_move_drop(PAWN, SQ_52, WHITE)).second, 4);
}
