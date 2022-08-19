#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "../../../thread.h"
#include "../hands.hpp"
#include "test_lib.hpp"

namespace {
constexpr Hand kFullHand = static_cast<Hand>(HAND_BIT_MASK);
}  // namespace

TEST(HandsTest, RemoveHand) {
  auto h1 = MakeHand<PAWN, PAWN, PAWN, LANCE, LANCE, LANCE, SILVER>();
  EXPECT_EQ(h1, (MakeHand<PAWN, PAWN, PAWN, LANCE, LANCE, LANCE, SILVER>()));
  komori::RemoveHand(h1, LANCE);
  EXPECT_EQ(h1, (MakeHand<PAWN, PAWN, PAWN, SILVER>()));
}

TEST(HandsTest, MergeHand) {
  const auto h1 = MakeHand<PAWN, PAWN, LANCE, SILVER>();
  const auto h2 = MakeHand<PAWN, SILVER, GOLD, GOLD>();
  const auto hand = komori::MergeHand(h1, h2);
  EXPECT_EQ(hand, (MakeHand<PAWN, PAWN, PAWN, LANCE, SILVER, SILVER, GOLD, GOLD>()));
}

TEST(HandsTest, CollectHand) {
  TestNode n{"4k4/ppppppppp/nn7/1s7/gg7/b8/9/9/9 b R3S3L4Prb2g2nl5p 1", true};

  const auto hand = komori::CollectHand(n->Pos());
  EXPECT_EQ(hand_count(hand, PAWN), 9);
  EXPECT_EQ(hand_count(hand, LANCE), 4);
  EXPECT_EQ(hand_count(hand, KNIGHT), 2);
  EXPECT_EQ(hand_count(hand, SILVER), 3);
  EXPECT_EQ(hand_count(hand, GOLD), 2);
  EXPECT_EQ(hand_count(hand, BISHOP), 1);
  EXPECT_EQ(hand_count(hand, ROOK), 2);
}

TEST(HandsTest, CountHand) {
  const auto hand = MakeHand<PAWN, PAWN, PAWN, LANCE, LANCE, LANCE, SILVER>();
  EXPECT_EQ(komori::CountHand(hand), 7);
}

TEST(HandsTest, AfterHand) {
  TestNode n{"4k4/3l5/3PP4/9/9/9/9/9/9 b L2r2b4g4s4n2l16p 1", true};

  Hand orig = n->OrHand();
  Hand orig_minus_lance = orig;
  sub_hand(orig_minus_lance, LANCE);
  Hand orig_plus_lance = orig;
  add_hand(orig_plus_lance, LANCE);
  EXPECT_EQ(komori::AfterHand(n.Pos(), make_move_drop(LANCE, SQ_52, BLACK), orig), orig_minus_lance);
  EXPECT_EQ(komori::AfterHand(n.Pos(), make_move_promote(SQ_63, SQ_62, B_PAWN), orig), orig_plus_lance);
  EXPECT_EQ(komori::AfterHand(n.Pos(), make_move_promote(SQ_53, SQ_52, B_PAWN), orig), orig);
  // overflow
  EXPECT_EQ(komori::AfterHand(n.Pos(), make_move_promote(SQ_63, SQ_62, B_PAWN), kFullHand), kFullHand);
}

TEST(HandsTest, BeforeHand) {
  TestNode n{"4k4/3l5/3PP4/9/9/9/9/9/9 b L2r2b4g4s4n2l16p 1", true};

  Hand orig = n->OrHand();
  Hand orig_minus_lance = orig;
  sub_hand(orig_minus_lance, LANCE);
  Hand orig_plus_lance = orig;
  add_hand(orig_plus_lance, LANCE);
  EXPECT_EQ(komori::BeforeHand(n.Pos(), make_move_drop(LANCE, SQ_52, BLACK), orig_minus_lance), orig);
  EXPECT_EQ(komori::BeforeHand(n.Pos(), make_move_promote(SQ_63, SQ_62, B_PAWN), orig_plus_lance), orig);
  EXPECT_EQ(komori::BeforeHand(n.Pos(), make_move_promote(SQ_53, SQ_52, B_PAWN), orig), orig);
  // overflow drop
  EXPECT_EQ(komori::BeforeHand(n.Pos(), make_move_drop(LANCE, SQ_52, BLACK), kFullHand), kFullHand);
  // overflow capture
  EXPECT_EQ(komori::BeforeHand(n.Pos(), make_move_promote(SQ_63, SQ_62, B_PAWN), HAND_ZERO), HAND_ZERO);
}

TEST(HandsTest, RemoveIfHandGivesOtherChecks) {
  TestNode n{"8k/9/8P/9/9/9/9/9/9 b NLP2r2b4g4s3n3l16p 1", true};

  const auto hand = komori::RemoveIfHandGivesOtherChecks(n.Pos(), kFullHand);
  EXPECT_TRUE(hand_exists(hand, PAWN));
  EXPECT_TRUE(hand_exists(hand, LANCE));
  EXPECT_TRUE(hand_exists(hand, KNIGHT));
  EXPECT_FALSE(hand_exists(hand, SILVER));
  EXPECT_FALSE(hand_exists(hand, GOLD));
  EXPECT_FALSE(hand_exists(hand, BISHOP));
  EXPECT_FALSE(hand_exists(hand, ROOK));
}

TEST(HandsTest, AddIfHandGivesOtherEvasions) {
  TestNode n{"9/9/9/7l1/nsns3pk/rbng3l1/rbng5/gssg3+P1/8L w 16Pl 1", false};

  const auto h1 = komori::AddIfHandGivesOtherEvasions(n.Pos(), HAND_ZERO);
  EXPECT_TRUE(hand_exists(h1, PAWN));
  EXPECT_FALSE(hand_exists(h1, LANCE));

  // double pawn
  TestNode n2{"8p/9/9/7l1/nsns3pk/rbng3l1/rbng5/gssg3+P1/8L w 15Pl 1", false};
  const auto h2 = komori::AddIfHandGivesOtherEvasions(n2.Pos(), HAND_ZERO);
  EXPECT_FALSE(hand_exists(h2, PAWN));
  EXPECT_FALSE(hand_exists(h2, LANCE));

  // double check
  TestNode n3("9/9/9/7l1/nsns3pk/rbng3l1/rb1g3N1/gssg3+P1/8L w 16Pl 1", false);
  const auto h3 = komori::AddIfHandGivesOtherEvasions(n3.Pos(), HAND_ZERO);
  EXPECT_FALSE(hand_exists(h3, PAWN));
  EXPECT_FALSE(hand_exists(h3, LANCE));
}

TEST(HandsTest, HandSet_OrNode) {
  TestNode n{"8k/9/8P/9/9/9/9/9/9 b NLP2r2b4g4s3n3l16p 1", true};

  komori::HandSet hand_set{komori::DisproofHandTag{}};
  hand_set.Update(kFullHand);
  const auto hand = hand_set.Get(n.Pos());
  EXPECT_TRUE(hand_exists(hand, PAWN));
  EXPECT_TRUE(hand_exists(hand, LANCE));
  EXPECT_TRUE(hand_exists(hand, KNIGHT));
  EXPECT_FALSE(hand_exists(hand, SILVER));
  EXPECT_FALSE(hand_exists(hand, GOLD));
  EXPECT_FALSE(hand_exists(hand, BISHOP));
  EXPECT_FALSE(hand_exists(hand, ROOK));
}

TEST(HandsTest, HandSet_AndNode) {
  TestNode n{"9/9/9/7l1/nsns3pk/rbng3l1/rbng5/gssg3+P1/8L w 16Pl 1", false};

  komori::HandSet hand_set{komori::ProofHandTag{}};
  hand_set.Update(HAND_ZERO);
  const auto hand = hand_set.Get(n.Pos());
  EXPECT_TRUE(hand_exists(hand, PAWN));
  EXPECT_FALSE(hand_exists(hand, LANCE));
}
