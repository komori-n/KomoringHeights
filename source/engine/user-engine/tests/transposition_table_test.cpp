#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sstream>
#include <unordered_set>

#include "../transposition_table.hpp"
#include "test_lib.hpp"

using komori::BitSet64;
using komori::kDepthMax;
using komori::tt::CircularEntryPointer;
using komori::tt::RepetitionTable;
using komori::tt::detail::kRegularRepetitionRatio;
using komori::tt::detail::TranspositionTableImpl;
using testing::Return;

namespace {

struct RegularTableMock {
  MOCK_METHOD(void, Resize, (std::uint64_t));
  MOCK_METHOD(void, Clear, ());
  MOCK_METHOD(komori::tt::CircularEntryPointer, PointerOf, (Key));
  MOCK_METHOD(double, CalculateHashRate, (), (const));
  MOCK_METHOD(void, CollectGarbage, (double));
  MOCK_METHOD(std::ostream&, Save, (std::ostream&));
  MOCK_METHOD(std::istream&, Load, (std::istream&));
  MOCK_METHOD(std::uint64_t, Capacity, (), (const));
  MOCK_METHOD(komori::tt::Entry*, begin, (), (const));
  MOCK_METHOD(komori::tt::Entry*, end, (), (const));
};

struct RepetitionTableMock {
  MOCK_METHOD(void, SetTableSizeMax, (std::uint64_t));
  MOCK_METHOD(void, Clear, ());
  MOCK_METHOD(double, HashRate, (), (const));
};

struct QueryMock {
  RepetitionTableMock& rep_table;
  CircularEntryPointer initial_entry_pointer;
  Key path_key;
  Key board_key;
  Hand hand;
  Depth depth;
};

class TranspositionTableTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EXPECT_CALL(tt_.GetRegularTable(), Resize);
    EXPECT_CALL(tt_.GetRepetitionTable(), SetTableSizeMax);
    tt_.Resize(1);
  }

  TranspositionTableImpl<QueryMock, RegularTableMock, RepetitionTableMock> tt_;
};
}  // namespace

TEST_F(TranspositionTableTest, Resize) {
  const std::uint64_t usi_hash_mb = 334;

  std::uint64_t n = 1;
  std::uint64_t m = 1;
  EXPECT_CALL(tt_.GetRegularTable(), Resize).WillOnce([&](std::uint64_t a) { n = a; });
  EXPECT_CALL(tt_.GetRepetitionTable(), SetTableSizeMax).WillOnce([&](std::uint64_t b) { m = b; });
  tt_.Resize(usi_hash_mb);

  EXPECT_FLOAT_EQ((1 - kRegularRepetitionRatio) * n * sizeof(komori::tt::Entry),
                  kRegularRepetitionRatio * m * sizeof(Key) * 6);
}

TEST_F(TranspositionTableTest, NewSearch) {
  EXPECT_CALL(tt_.GetRepetitionTable(), Clear).Times(1);
  tt_.NewSearch();
}

TEST_F(TranspositionTableTest, Clear) {
  EXPECT_CALL(tt_.GetRegularTable(), Clear).Times(1);
  EXPECT_CALL(tt_.GetRepetitionTable(), Clear).Times(1);
  tt_.Clear();
}

TEST_F(TranspositionTableTest, BuildQuery) {
  TestNode test_node{"4k4/9/4G4/9/9/9/9/9/9 b P2r2b3g4s4n4l17p 1", true};

  EXPECT_CALL(tt_.GetRegularTable(), PointerOf).WillOnce(Return(CircularEntryPointer{nullptr, nullptr, nullptr}));
  const auto query = tt_.BuildQuery(*test_node);

  EXPECT_EQ(&query.rep_table, &tt_.GetRepetitionTable());
  EXPECT_EQ(query.initial_entry_pointer.data(), nullptr);
  EXPECT_EQ(query.path_key, test_node->GetPathKey());
  EXPECT_EQ(query.board_key, test_node->Pos().state()->board_key());
  EXPECT_EQ(query.hand, test_node->OrHand());
  EXPECT_EQ(query.depth, test_node->GetDepth());
}

TEST_F(TranspositionTableTest, BuildChildQuery) {
  TestNode test_node{"4k4/4+P4/9/9/9/9/9/9/9 w P2r2b4g4s4n4l16p 1", false};
  const Move move = make_move(SQ_51, SQ_52, W_KING);

  EXPECT_CALL(tt_.GetRegularTable(), PointerOf).WillOnce(Return(CircularEntryPointer{nullptr, nullptr, nullptr}));
  const auto query = tt_.BuildChildQuery(*test_node, move);

  EXPECT_EQ(&query.rep_table, &tt_.GetRepetitionTable());
  EXPECT_EQ(query.initial_entry_pointer.data(), nullptr);
  EXPECT_EQ(query.path_key, test_node->PathKeyAfter(move));
  EXPECT_EQ(query.board_key, test_node->Pos().board_key_after(move));
  EXPECT_EQ(query.hand, test_node->OrHandAfter(move));
  EXPECT_EQ(query.depth, test_node->GetDepth() + 1);
}

TEST_F(TranspositionTableTest, BuildQueryByKey_Normal) {
  const Key board_key = 0x334334334334;
  const Key path_key = 0x264264264264;
  const auto hand = MakeHand<PAWN, LANCE, LANCE>();

  EXPECT_CALL(tt_.GetRegularTable(), PointerOf).WillOnce(Return(CircularEntryPointer{nullptr, nullptr, nullptr}));
  const auto query = tt_.BuildQueryByKey({board_key, hand}, path_key);

  EXPECT_EQ(&query.rep_table, &tt_.GetRepetitionTable());
  EXPECT_EQ(query.initial_entry_pointer.data(), nullptr);
  EXPECT_EQ(query.path_key, path_key);
  EXPECT_EQ(query.board_key, board_key);
  EXPECT_EQ(query.hand, hand);
  EXPECT_EQ(query.depth, kDepthMax);
}

TEST_F(TranspositionTableTest, Hashfull) {
  const double r1 = 0.75;
  const double r2 = 0.5;
  EXPECT_CALL(tt_.GetRegularTable(), CalculateHashRate).WillOnce(Return(r1));
  EXPECT_CALL(tt_.GetRepetitionTable(), HashRate).WillOnce(Return(r2));

  EXPECT_FLOAT_EQ(tt_.Hashfull(), 1000 * (r1 * kRegularRepetitionRatio + r2 * (1 - kRegularRepetitionRatio)));
}

TEST_F(TranspositionTableTest, CollectGarbage) {
  EXPECT_CALL(tt_.GetRegularTable(), CollectGarbage(0.334)).Times(1);
  tt_.CollectGarbage(0.334);
}

TEST_F(TranspositionTableTest, Capacity) {
  EXPECT_CALL(tt_.GetRegularTable(), Capacity()).WillOnce(Return(334));
  EXPECT_EQ(tt_.Capacity(), 334);
}
