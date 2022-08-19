#include <gtest/gtest.h>

#include "test_lib.hpp"

TEST(NodeTest, PositionValues) {
  TestNode n{"ln1gkg1nl/6+P2/2sppps1p/2p3p2/p8/P1P1P3P/2NP1PP2/3s1KSR1/L1+b2G1NL w R2Pbgp 1", true};
  Position& p = n->Pos();

  EXPECT_EQ(&n->Pos(), &p);
  EXPECT_EQ(n->Us(), WHITE);
  EXPECT_EQ(n->OrColor(), WHITE);
  EXPECT_EQ(n->AndColor(), BLACK);
  EXPECT_TRUE(n->IsOrNode());
  EXPECT_EQ(n->OrHand(), p.hand_of(WHITE));
  EXPECT_EQ(n->AndHand(), p.hand_of(BLACK));
  EXPECT_EQ(n->GetDepth(), 4);
  EXPECT_EQ(n->GetKey(), p.key());
  EXPECT_EQ(n->BoardKey(), p.state()->board_key());
  EXPECT_EQ(n->GetPathKey(), 33);

  const Move m = make_move_drop(BISHOP, SQ_57, WHITE);
  EXPECT_EQ(n->KeyAfter(m), p.key_after(m));
  EXPECT_EQ(n->BoardKeyAfter(m), p.board_key_after(m));
  EXPECT_EQ(n->PathKeyAfter(m), komori::PathKeyAfter(33, m, 4));
  EXPECT_EQ(n->OrHandAfter(m), komori::AfterHand(p, m, p.hand_of(WHITE)));
}

TEST(NodeTest, Repetitions) {
  TestNode n{"ln1gkg1nl/6+P2/2sppps1p/2p3p2/p8/P1P1P3P/2NP1PP2/3s1KSR1/L1+b2G1NL w R2Pbgp 1", true};

  const auto m = make_move(SQ_59, SQ_48, B_KING);

  EXPECT_FALSE(n->IsRepetition());
  EXPECT_FALSE(n->IsRepetitionAfter(m));
  EXPECT_FALSE(n->ContainsInPath(n->BoardKeyAfter(m), n->OrHandAfter(m)));
  EXPECT_FALSE(n->ContainsInPath(n->BoardKeyAfter(m)));

  n->DoMove(make_move(SQ_68, SQ_57, W_SILVER));
  n->DoMove(make_move(SQ_48, SQ_59, B_KING));
  n->DoMove(make_move(SQ_57, SQ_68, W_SILVER));

  EXPECT_TRUE(n->IsRepetitionAfter(m));
  EXPECT_TRUE(n->ContainsInPath(n->BoardKeyAfter(m), n->OrHandAfter(m)));
  EXPECT_TRUE(n->ContainsInPath(n->BoardKeyAfter(m)));

  n->DoMove(m);

  EXPECT_TRUE(n->IsRepetition());
}

TEST(NodeTest, InferiorLoop) {
  TestNode n{"ln1gkg1nl/6+P2/2sppps1p/2p3p2/p8/P1P1P3P/2NP1PP2/3s1KSR1/L1+b2G1NL w R2Pbgp 1", true};

  const auto m = make_move(SQ_39, SQ_48, B_KING);

  EXPECT_FALSE(n->IsRepetitionOrInferior());
  EXPECT_FALSE(n->IsRepetitionOrInferiorAfter(m));

  n->DoMove(make_move_drop(BISHOP, SQ_39, WHITE));
  n->DoMove(make_move(SQ_48, SQ_39, B_KING));
  n->DoMove(make_move_drop(GOLD, SQ_48, WHITE));

  EXPECT_TRUE(n->IsRepetitionOrInferiorAfter(m));

  n->DoMove(m);

  EXPECT_TRUE(n->IsRepetitionOrInferior());
}

TEST(NodeTest, SuperiorLoop) {
  TestNode n{"4k4/3p1R3/2B3B2/9/9/9/9/9/9 b r4g4s4n4l17p 1", true};

  const auto m = make_move_drop(PAWN, SQ_62, WHITE);

  EXPECT_FALSE(n->IsRepetitionOrSuperior());
  EXPECT_FALSE(n->IsRepetitionOrSuperiorAfter(m));

  n->DoMove(make_move(SQ_42, SQ_62, B_ROOK));
  n->DoMove(make_move_drop(PAWN, SQ_42, WHITE));
  n->DoMove(make_move(SQ_62, SQ_42, B_ROOK));

  EXPECT_TRUE(n->IsRepetitionOrSuperiorAfter(m));

  n->DoMove(m);

  EXPECT_TRUE(n->IsRepetitionOrSuperior());
}

TEST(NodeTest, RollForward) {
  TestNode n{"ln1gkg1nl/6+P2/2sppps1p/2p3p2/p8/P1P1P3P/2NP1PP2/3s1KSR1/L1+b2G1NL w R2Pbgp 1", true};

  const auto board_key = n->BoardKey();

  RollForward(*n, {make_move_drop(BISHOP, SQ_39, WHITE), make_move(SQ_48, SQ_39, B_KING),
                   make_move_drop(GOLD, SQ_48, WHITE), make_move(SQ_39, SQ_48, B_KING)});

  EXPECT_EQ(n->BoardKey(), board_key);
  EXPECT_TRUE(n->IsRepetitionOrInferior());

  RollBack(*n, {make_move_drop(BISHOP, SQ_39, WHITE), make_move(SQ_48, SQ_39, B_KING),
                make_move_drop(GOLD, SQ_48, WHITE), make_move(SQ_39, SQ_48, B_KING)});

  EXPECT_EQ(n->BoardKey(), board_key);
  EXPECT_FALSE(n->IsRepetitionOrInferior());
}

TEST(CheckMate1PlyTest, Mate) {
  TestNode n{"4k4/9/4P4/9/9/9/9/9/9 b 2R2B4G4S4N4L17P 1", true};

  const auto [best_move, proof_hand] = komori::CheckMate1Ply(*n);
  EXPECT_EQ(best_move, make_move_drop(GOLD, SQ_52, BLACK));
  EXPECT_EQ(proof_hand, (MakeHand<GOLD>()));
}

TEST(CheckMate1PlyTest, InCheck) {
  TestNode n{"4k4/9/4P4/9/9/9/9/9/9 b S2r2b4g3s4n4l17p 1", true};

  const auto [best_move, proof_hand] = komori::CheckMate1Ply(*n);
  EXPECT_EQ(best_move, MOVE_NONE);
  EXPECT_EQ(proof_hand, komori::kNullHand);
}

TEST(CheckMate1PlyTest, NoCheckmate) {
  TestNode n{"4k4/9/4P4/9/9/9/9/4p4/4K4 b G2r2b3g4s4n4l16p 1", true};

  const auto [best_move, proof_hand] = komori::CheckMate1Ply(*n);
  EXPECT_EQ(best_move, MOVE_NONE);
  EXPECT_EQ(proof_hand, komori::kNullHand);
}
