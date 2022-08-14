#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "../../../thread.h"
#include "../hands.hpp"
#include "../node.hpp"

namespace {
constexpr Hand kFullHand = static_cast<Hand>(HAND_BIT_MASK);

class HandsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    h1_ = HAND_ZERO;
    add_hand(h1_, PAWN, 10);
    add_hand(h1_, LANCE, 3);

    h2_ = HAND_ZERO;
    add_hand(h2_, PAWN, 3);
    add_hand(h2_, KNIGHT, 2);
  }

  void Init(const std::string& sfen, bool or_node = true) {
    p_.set(sfen, &si_, Threads[0]);
    n_ = std::make_unique<komori::Node>(p_, or_node);
  }

  Hand h1_, h2_;
  Position p_;
  StateInfo si_;
  std::unique_ptr<komori::Node> n_;
};
}  // namespace

TEST_F(HandsTest, RemoveHand) {
  EXPECT_EQ(hand_count(h1_, PAWN), 10);
  EXPECT_EQ(hand_count(h1_, LANCE), 3);
  komori::RemoveHand(h1_, LANCE);
  EXPECT_EQ(hand_count(h1_, PAWN), 10);
  EXPECT_EQ(hand_count(h1_, LANCE), 0);
}

TEST_F(HandsTest, MergeHand) {
  const auto hand = komori::MergeHand(h1_, h2_);
  EXPECT_EQ(hand_count(hand, PAWN), 13);
  EXPECT_EQ(hand_count(hand, LANCE), 3);
  EXPECT_EQ(hand_count(hand, KNIGHT), 2);
}

TEST_F(HandsTest, CollectHand) {
  Init("4k4/ppppppppp/nn7/1s7/gg7/b8/9/9/9 b R3S3L4Prb2g2nl5p 1");

  const auto hand = komori::CollectHand(n_->Pos());
  EXPECT_EQ(hand_count(hand, PAWN), 9);
  EXPECT_EQ(hand_count(hand, LANCE), 4);
  EXPECT_EQ(hand_count(hand, KNIGHT), 2);
  EXPECT_EQ(hand_count(hand, SILVER), 3);
  EXPECT_EQ(hand_count(hand, GOLD), 2);
  EXPECT_EQ(hand_count(hand, BISHOP), 1);
  EXPECT_EQ(hand_count(hand, ROOK), 2);
}

TEST_F(HandsTest, CountHand) {
  EXPECT_EQ(komori::CountHand(h1_), 13);
  EXPECT_EQ(komori::CountHand(h2_), 5);
}

TEST_F(HandsTest, AfterHand) {
  Init("4k4/3l5/3PP4/9/9/9/9/9/9 b L2r2b4g4s4n2l16p 1");

  Hand orig = n_->OrHand();
  Hand orig_minus_lance = orig;
  sub_hand(orig_minus_lance, LANCE);
  Hand orig_plus_lance = orig;
  add_hand(orig_plus_lance, LANCE);
  EXPECT_EQ(komori::AfterHand(p_, make_move_drop(LANCE, SQ_52, BLACK), orig), orig_minus_lance);
  EXPECT_EQ(komori::AfterHand(p_, make_move_promote(SQ_63, SQ_62, B_PAWN), orig), orig_plus_lance);
  EXPECT_EQ(komori::AfterHand(p_, make_move_promote(SQ_53, SQ_52, B_PAWN), orig), orig);
  // overflow
  EXPECT_EQ(komori::AfterHand(p_, make_move_promote(SQ_63, SQ_62, B_PAWN), kFullHand), kFullHand);
}

TEST_F(HandsTest, BeforeHand) {
  Init("4k4/3l5/3PP4/9/9/9/9/9/9 b L2r2b4g4s4n2l16p 1");

  Hand orig = n_->OrHand();
  Hand orig_minus_lance = orig;
  sub_hand(orig_minus_lance, LANCE);
  Hand orig_plus_lance = orig;
  add_hand(orig_plus_lance, LANCE);
  EXPECT_EQ(komori::BeforeHand(p_, make_move_drop(LANCE, SQ_52, BLACK), orig_minus_lance), orig);
  EXPECT_EQ(komori::BeforeHand(p_, make_move_promote(SQ_63, SQ_62, B_PAWN), orig_plus_lance), orig);
  EXPECT_EQ(komori::BeforeHand(p_, make_move_promote(SQ_53, SQ_52, B_PAWN), orig), orig);
  // overflow drop
  EXPECT_EQ(komori::BeforeHand(p_, make_move_drop(LANCE, SQ_52, BLACK), kFullHand), kFullHand);
  // overflow capture
  EXPECT_EQ(komori::BeforeHand(p_, make_move_promote(SQ_63, SQ_62, B_PAWN), HAND_ZERO), HAND_ZERO);
}

TEST_F(HandsTest, RemoveIfHandGivesOtherChecks) {
  Init("8k/9/8P/9/9/9/9/9/9 b NLP2r2b4g4s3n3l16p 1");

  const auto hand = komori::RemoveIfHandGivesOtherChecks(p_, kFullHand);
  EXPECT_TRUE(hand_exists(hand, PAWN));
  EXPECT_TRUE(hand_exists(hand, LANCE));
  EXPECT_TRUE(hand_exists(hand, KNIGHT));
  EXPECT_FALSE(hand_exists(hand, SILVER));
  EXPECT_FALSE(hand_exists(hand, GOLD));
  EXPECT_FALSE(hand_exists(hand, BISHOP));
  EXPECT_FALSE(hand_exists(hand, ROOK));
}

TEST_F(HandsTest, AddIfHandGivesOtherEvasions) {
  Init("9/9/9/7l1/nsns3pk/rbng3l1/rbng5/gssg3+P1/8L w 16Pl 1", false);

  const auto h1 = komori::AddIfHandGivesOtherEvasions(p_, HAND_ZERO);
  EXPECT_TRUE(hand_exists(h1, PAWN));
  EXPECT_FALSE(hand_exists(h1, LANCE));

  // double pawn
  Init("8p/9/9/7l1/nsns3pk/rbng3l1/rbng5/gssg3+P1/8L w 15Pl 1", false);
  const auto h2 = komori::AddIfHandGivesOtherEvasions(p_, HAND_ZERO);
  EXPECT_FALSE(hand_exists(h2, PAWN));
  EXPECT_FALSE(hand_exists(h2, LANCE));

  // double check
  Init("9/9/9/7l1/nsns3pk/rbng3l1/rb1g3N1/gssg3+P1/8L w 16Pl 1", false);
  const auto h3 = komori::AddIfHandGivesOtherEvasions(p_, HAND_ZERO);
  EXPECT_FALSE(hand_exists(h3, PAWN));
  EXPECT_FALSE(hand_exists(h3, LANCE));
}

TEST_F(HandsTest, HandSet_OrNode) {
  Init("8k/9/8P/9/9/9/9/9/9 b NLP2r2b4g4s3n3l16p 1");

  komori::HandSet hand_set{komori::DisproofHandTag{}};
  hand_set.Update(kFullHand);
  const auto hand = hand_set.Get(p_);
  EXPECT_TRUE(hand_exists(hand, PAWN));
  EXPECT_TRUE(hand_exists(hand, LANCE));
  EXPECT_TRUE(hand_exists(hand, KNIGHT));
  EXPECT_FALSE(hand_exists(hand, SILVER));
  EXPECT_FALSE(hand_exists(hand, GOLD));
  EXPECT_FALSE(hand_exists(hand, BISHOP));
  EXPECT_FALSE(hand_exists(hand, ROOK));
}

TEST_F(HandsTest, HandSet_AndNode) {
  Init("9/9/9/7l1/nsns3pk/rbng3l1/rbng5/gssg3+P1/8L w 16Pl 1", false);

  komori::HandSet hand_set{komori::ProofHandTag{}};
  hand_set.Update(HAND_ZERO);
  const auto hand = hand_set.Get(p_);
  EXPECT_TRUE(hand_exists(hand, PAWN));
  EXPECT_FALSE(hand_exists(hand, LANCE));
}
