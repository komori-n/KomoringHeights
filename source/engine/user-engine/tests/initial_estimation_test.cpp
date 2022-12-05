#include <gtest/gtest.h>

#include <memory>
#include <string>

#define USE_DFPN_PLUS
#include "../initial_estimation.hpp"
#include "test_lib.hpp"

using komori::InitialPnDn;
using komori::IsSumDeltaNode;

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

TEST(InitialEstimationTest, IsSumNode_OrDrop) {
  TestNode n{"4k4/9/9/9/9/9/9/9/9 b RBGSNLPrb3g3s3n3l17p 1", true};

  EXPECT_TRUE(IsSumDeltaNode(*n, make_move_drop(PAWN, SQ_52, BLACK)));
  EXPECT_FALSE(IsSumDeltaNode(*n, make_move_drop(LANCE, SQ_52, BLACK)));
  EXPECT_TRUE(IsSumDeltaNode(*n, make_move_drop(KNIGHT, SQ_43, BLACK)));
  EXPECT_TRUE(IsSumDeltaNode(*n, make_move_drop(SILVER, SQ_52, BLACK)));
  EXPECT_TRUE(IsSumDeltaNode(*n, make_move_drop(GOLD, SQ_52, BLACK)));
  EXPECT_FALSE(IsSumDeltaNode(*n, make_move_drop(BISHOP, SQ_42, BLACK)));
  EXPECT_FALSE(IsSumDeltaNode(*n, make_move_drop(ROOK, SQ_52, BLACK)));
}

TEST(InitialEstimationTest, IsSumNode_AndDrop) {
  TestNode n{"9/9/9/9/k7R/9/9/9/9 w r2b4g4s4n4l18p 1", false};

  EXPECT_TRUE(IsSumDeltaNode(*n, make_move_drop(PAWN, SQ_85, WHITE)));
  EXPECT_TRUE(IsSumDeltaNode(*n, make_move_drop(LANCE, SQ_85, WHITE)));
  EXPECT_TRUE(IsSumDeltaNode(*n, make_move_drop(KNIGHT, SQ_85, WHITE)));
  EXPECT_TRUE(IsSumDeltaNode(*n, make_move_drop(SILVER, SQ_85, WHITE)));
  EXPECT_TRUE(IsSumDeltaNode(*n, make_move_drop(GOLD, SQ_85, WHITE)));
  EXPECT_TRUE(IsSumDeltaNode(*n, make_move_drop(BISHOP, SQ_85, WHITE)));
  EXPECT_TRUE(IsSumDeltaNode(*n, make_move_drop(ROOK, SQ_85, WHITE)));
}

TEST(InitialEstimationTest, IsSumNode_OrLance) {
  TestNode n1{"9/8k/8p/9/9/9/9/9/8L b 2r2b4g4s4n3l17p 1", true};

  EXPECT_FALSE(IsSumDeltaNode(*n1, make_move_promote(SQ_19, SQ_13, B_LANCE)));
  EXPECT_FALSE(IsSumDeltaNode(*n1, make_move(SQ_19, SQ_13, B_LANCE)));

  TestNode n2{"8k/8p/9/9/9/9/9/9/8L b 2r2b4g4s4n3l17p 1", true};

  EXPECT_TRUE(IsSumDeltaNode(*n2, make_move_promote(SQ_19, SQ_12, B_LANCE)));
  EXPECT_TRUE(IsSumDeltaNode(*n2, make_move(SQ_19, SQ_12, B_LANCE)));

  TestNode n3{"9/7k1/8p/9/9/9/9/9/8L b 2r2b4g4s4n3l17p 1", true};
  EXPECT_TRUE(IsSumDeltaNode(*n3, make_move_promote(SQ_19, SQ_13, B_LANCE)));

  TestNode n4{"9/9/8k/8p/9/9/9/9/8L b 2r2b4g4s4n3l17p 1", true};
  EXPECT_TRUE(IsSumDeltaNode(*n4, make_move(SQ_19, SQ_14, B_LANCE)));

  TestNode n5{"8l/9/9/9/9/9/8P/8K/9 w 2r2b4g4s4n3l17p 1", true};

  EXPECT_FALSE(IsSumDeltaNode(*n5, make_move_promote(SQ_11, SQ_17, W_LANCE)));
  EXPECT_FALSE(IsSumDeltaNode(*n5, make_move(SQ_11, SQ_17, W_LANCE)));

  TestNode n6{"8l/9/9/9/9/9/9/8P/8K w 2r2b4g4s4n3l17p 1", true};

  EXPECT_TRUE(IsSumDeltaNode(*n6, make_move_promote(SQ_11, SQ_18, W_LANCE)));
  EXPECT_TRUE(IsSumDeltaNode(*n6, make_move(SQ_11, SQ_18, W_LANCE)));

  TestNode n7{"8l/9/9/9/9/9/8P/7K1/9 w 2r2b4g4s4n3l17p 1", true};
  EXPECT_TRUE(IsSumDeltaNode(*n7, make_move_promote(SQ_11, SQ_17, W_LANCE)));

  TestNode n8{"8l/9/9/9/9/8P/8K/9/9 w 2r2b4g4s4n3l17p 1", true};
  EXPECT_TRUE(IsSumDeltaNode(*n8, make_move(SQ_11, SQ_16, W_LANCE)));
}
