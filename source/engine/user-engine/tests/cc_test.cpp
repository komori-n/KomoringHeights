#include <gtest/gtest.h>

#include <memory>

#include "../../thread.h"
#include "new_cc.hpp"

TEST(IndexTable, Push) {
  komori::detail::IndexTable idx;

  EXPECT_EQ(idx.Push(2), 0);
  EXPECT_EQ(idx.Push(6), 1);
  EXPECT_EQ(idx.Push(4), 2);
}

TEST(IndexTable, Pop) {
  komori::detail::IndexTable idx;

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);

  EXPECT_EQ(idx.size(), 3);
  idx.Pop();
  EXPECT_EQ(idx.size(), 2);
}

TEST(IndexTable, operator) {
  komori::detail::IndexTable idx;

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);

  EXPECT_EQ(idx[0], 2);
  EXPECT_EQ(idx[1], 6);
  EXPECT_EQ(idx[2], 4);
}

TEST(IndexTable, iterators) {
  komori::detail::IndexTable idx;

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);
  EXPECT_EQ(*idx.begin(), 2);
  EXPECT_EQ(idx.begin() + 3, idx.end());
  EXPECT_EQ(*idx.begin(), idx.front());
}

TEST(IndexTable, size) {
  komori::detail::IndexTable idx;
  EXPECT_TRUE(idx.empty());
  EXPECT_EQ(idx.size(), 0);

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);
  EXPECT_FALSE(idx.empty());
  EXPECT_EQ(idx.size(), 3);
}

namespace {
class DelayedMovesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    or_p_.set_hirate(&or_si_, Threads[0]);
    or_n_ = std::make_unique<komori::Node>(or_p_, true);

    and_p_.set_hirate(&and_si_, Threads[0]);
    and_n_ = std::make_unique<komori::Node>(and_p_, true);
    and_n_->DoMove(make_move(SQ_17, SQ_16, B_PAWN));
  }

  Position or_p_, and_p_;
  StateInfo or_si_, and_si_;
  std::unique_ptr<komori::Node> or_n_;
  std::unique_ptr<komori::Node> and_n_;
};
}  // namespace

TEST_F(DelayedMovesTest, Empty) {
  komori::detail::DelayedMoves delayed_moves{*or_n_};

  EXPECT_FALSE(delayed_moves.Add(make_move(SQ_57, SQ_55, B_PAWN), 0));
  EXPECT_FALSE(delayed_moves.Add(make_move_drop(PAWN, SQ_55, BLACK), 0));
}

TEST_F(DelayedMovesTest, OrDrop) {
  komori::detail::DelayedMoves delayed_moves{*or_n_};

  EXPECT_FALSE(delayed_moves.Add(make_move_drop(PAWN, SQ_55, BLACK), 0));
  EXPECT_FALSE(delayed_moves.Add(make_move_drop(LANCE, SQ_55, BLACK), 1));
  EXPECT_FALSE(delayed_moves.Add(make_move_drop(KNIGHT, SQ_55, BLACK), 2));
}

TEST_F(DelayedMovesTest, AndDrop) {
  komori::detail::DelayedMoves delayed_moves{*and_n_};

  EXPECT_FALSE(delayed_moves.Add(make_move_drop(PAWN, SQ_55, WHITE), 0));
  EXPECT_EQ(delayed_moves.Add(make_move_drop(LANCE, SQ_55, WHITE), 1), std::optional<std::uint32_t>{0});
  EXPECT_EQ(delayed_moves.Add(make_move_drop(KNIGHT, SQ_55, WHITE), 2), std::optional<std::uint32_t>{1});
  EXPECT_FALSE(delayed_moves.Add(make_move_drop(SILVER, SQ_56, WHITE), 0));
}

TEST_F(DelayedMovesTest, OrNoPromote) {
  komori::detail::DelayedMoves delayed_moves{*or_n_};

  EXPECT_FALSE(delayed_moves.Add(make_move(SQ_28, SQ_23, B_ROOK), 0));
  EXPECT_EQ(delayed_moves.Add(make_move_promote(SQ_28, SQ_23, B_ROOK), 1), std::optional<std::uint32_t>{0});
  EXPECT_FALSE(delayed_moves.Add(make_move_promote(SQ_13, SQ_23, B_ROOK), 2));
  EXPECT_FALSE(delayed_moves.Add(make_move(SQ_19, SQ_12, B_LANCE), 3));
  EXPECT_EQ(delayed_moves.Add(make_move_promote(SQ_19, SQ_12, B_LANCE), 4), std::optional<std::uint32_t>{3});
  EXPECT_FALSE(delayed_moves.Add(make_move(SQ_19, SQ_13, B_LANCE), 5));
  EXPECT_FALSE(delayed_moves.Add(make_move_promote(SQ_19, SQ_13, B_LANCE), 6));
}

TEST_F(DelayedMovesTest, AndNoPromote) {
  komori::detail::DelayedMoves delayed_moves{*and_n_};

  EXPECT_FALSE(delayed_moves.Add(make_move(SQ_82, SQ_87, W_ROOK), 0));
  EXPECT_EQ(delayed_moves.Add(make_move_promote(SQ_82, SQ_87, W_ROOK), 1), std::optional<std::uint32_t>{0});
  EXPECT_FALSE(delayed_moves.Add(make_move_promote(SQ_97, SQ_87, W_ROOK), 2));
  EXPECT_FALSE(delayed_moves.Add(make_move(SQ_91, SQ_98, W_LANCE), 3));
  EXPECT_EQ(delayed_moves.Add(make_move_promote(SQ_91, SQ_98, W_LANCE), 4), std::optional<std::uint32_t>{3});
  EXPECT_FALSE(delayed_moves.Add(make_move(SQ_91, SQ_97, W_LANCE), 5));
  EXPECT_FALSE(delayed_moves.Add(make_move_promote(SQ_91, SQ_97, W_LANCE), 6));
}