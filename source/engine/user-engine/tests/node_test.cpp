#include <gtest/gtest.h>

#include "../../thread.h"
#include "../node.hpp"

using komori::Node;

namespace {
class NodeTest : public ::testing::Test {
 protected:
  void SetUp() override { komori::PathKeyInit(); }

  void Init(const std::string& sfen, bool root_is_or_node) {
    p_.set(sfen, &si_, Threads[0]);
    n_ = std::make_unique<Node>(p_, root_is_or_node, 33, 4);
  }

  Position p_;
  StateInfo si_;
  std::unique_ptr<Node> n_;
};
}  // namespace

TEST_F(NodeTest, PositionValues) {
  Init("ln1gkg1nl/6+P2/2sppps1p/2p3p2/p8/P1P1P3P/2NP1PP2/3s1KSR1/L1+b2G1NL w R2Pbgp 1", true);

  EXPECT_EQ(&n_->Pos(), &p_);
  EXPECT_EQ(n_->Us(), WHITE);
  EXPECT_EQ(n_->OrColor(), WHITE);
  EXPECT_EQ(n_->AndColor(), BLACK);
  EXPECT_TRUE(n_->IsOrNode());
  EXPECT_EQ(n_->OrHand(), p_.hand_of(WHITE));
  EXPECT_EQ(n_->AndHand(), p_.hand_of(BLACK));
  EXPECT_EQ(n_->GetDepth(), 4);
  EXPECT_EQ(n_->GetKey(), p_.key());
  EXPECT_EQ(n_->BoardKey(), p_.state()->board_key());
  EXPECT_EQ(n_->GetPathKey(), 33);

  const Move m = make_move_drop(BISHOP, SQ_57, WHITE);
  EXPECT_EQ(n_->KeyAfter(m), p_.key_after(m));
  EXPECT_EQ(n_->BoardKeyAfter(m), p_.board_key_after(m));
  EXPECT_EQ(n_->PathKeyAfter(m), komori::PathKeyAfter(33, m, 4));
  EXPECT_EQ(n_->OrHandAfter(m), komori::AfterHand(p_, m, p_.hand_of(WHITE)));
}

TEST_F(NodeTest, Repetitions) {
  Init("ln1gkg1nl/6+P2/2sppps1p/2p3p2/p8/P1P1P3P/2NP1PP2/3s1KSR1/L1+b2G1NL w R2Pbgp 1", true);

  const auto m = make_move(SQ_59, SQ_48, B_KING);

  EXPECT_FALSE(n_->IsRepetition());
  EXPECT_FALSE(n_->IsRepetitionAfter(m));
  EXPECT_FALSE(n_->ContainsInPath(n_->BoardKeyAfter(m), n_->OrHandAfter(m)));
  EXPECT_FALSE(n_->ContainsInPath(n_->BoardKeyAfter(m)));

  n_->DoMove(make_move(SQ_68, SQ_57, W_SILVER));
  n_->DoMove(make_move(SQ_48, SQ_59, B_KING));
  n_->DoMove(make_move(SQ_57, SQ_68, W_SILVER));

  EXPECT_TRUE(n_->IsRepetitionAfter(m));
  EXPECT_TRUE(n_->ContainsInPath(n_->BoardKeyAfter(m), n_->OrHandAfter(m)));
  EXPECT_TRUE(n_->ContainsInPath(n_->BoardKeyAfter(m)));

  n_->DoMove(m);

  EXPECT_TRUE(n_->IsRepetition());
}

TEST_F(NodeTest, InferiorLoop) {
  Init("ln1gkg1nl/6+P2/2sppps1p/2p3p2/p8/P1P1P3P/2NP1PP2/3s1KSR1/L1+b2G1NL w R2Pbgp 1", true);

  const auto m = make_move(SQ_39, SQ_48, B_KING);

  EXPECT_FALSE(n_->IsRepetitionOrInferior());
  EXPECT_FALSE(n_->IsRepetitionOrInferiorAfter(m));

  n_->DoMove(make_move_drop(BISHOP, SQ_39, WHITE));
  n_->DoMove(make_move(SQ_48, SQ_39, B_KING));
  n_->DoMove(make_move_drop(GOLD, SQ_48, WHITE));

  EXPECT_TRUE(n_->IsRepetitionOrInferiorAfter(m));

  n_->DoMove(m);

  EXPECT_TRUE(n_->IsRepetitionOrInferior());
}

TEST_F(NodeTest, SuperiorLoop) {
  Init("4k4/3p1R3/2B3B2/9/9/9/9/9/9 b r4g4s4n4l17p 1", true);

  const auto m = make_move_drop(PAWN, SQ_62, WHITE);

  EXPECT_FALSE(n_->IsRepetitionOrSuperior());
  EXPECT_FALSE(n_->IsRepetitionOrSuperiorAfter(m));

  n_->DoMove(make_move(SQ_42, SQ_62, B_ROOK));
  n_->DoMove(make_move_drop(PAWN, SQ_42, WHITE));
  n_->DoMove(make_move(SQ_62, SQ_42, B_ROOK));

  EXPECT_TRUE(n_->IsRepetitionOrSuperiorAfter(m));

  n_->DoMove(m);

  EXPECT_TRUE(n_->IsRepetitionOrSuperior());
}

TEST_F(NodeTest, RollForward) {
  Init("ln1gkg1nl/6+P2/2sppps1p/2p3p2/p8/P1P1P3P/2NP1PP2/3s1KSR1/L1+b2G1NL w R2Pbgp 1", true);

  const auto board_key = n_->BoardKey();

  RollForward(*n_, {make_move_drop(BISHOP, SQ_39, WHITE), make_move(SQ_48, SQ_39, B_KING),
                    make_move_drop(GOLD, SQ_48, WHITE), make_move(SQ_39, SQ_48, B_KING)});

  EXPECT_EQ(n_->BoardKey(), board_key);
  EXPECT_TRUE(n_->IsRepetitionOrInferior());

  RollBack(*n_, {make_move_drop(BISHOP, SQ_39, WHITE), make_move(SQ_48, SQ_39, B_KING),
                 make_move_drop(GOLD, SQ_48, WHITE), make_move(SQ_39, SQ_48, B_KING)});

  EXPECT_EQ(n_->BoardKey(), board_key);
  EXPECT_FALSE(n_->IsRepetitionOrInferior());
}
