#include <gtest/gtest.h>

#include "new_ttentry.hpp"

using komori::kInfinitePnDn;
using komori::kMaxMateLen;
using komori::kMaxNumMateMoves;
using komori::kNullHand;
using komori::kNullKey;
using komori::MateLen;
using komori::PnDn;

namespace {
class EntryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    hand_p1_ = HAND_ZERO;
    add_hand(hand_p1_, PAWN);

    hand_p2_ = HAND_ZERO;
    add_hand(hand_p2_, PAWN, 2);
  }

  void Init(Key board_key, Hand hand) { entry_.Init(board_key, hand); }

  void LookUp(Hand hand,
              Depth depth,
              MateLen len,
              bool expected_res,
              PnDn expected_pn,
              PnDn expected_dn,
              MateLen expected_len,
              int line) {
    PnDn pn = 1;
    PnDn dn = 1;
    const auto res = entry_.LookUp(hand, depth, len, pn, dn);

    EXPECT_EQ(res, expected_res) << "LINE: " << line;
    EXPECT_EQ(pn, expected_pn) << "LINE: " << line;
    EXPECT_EQ(dn, expected_dn) << "LINE: " << line;
    EXPECT_EQ(len, expected_len) << "LINE: " << line;
  }

  Hand hand_p1_;
  Hand hand_p2_;
  komori::tt::detail::Entry entry_;
};
}  // namespace

TEST_F(EntryTest, Init) {
  Init(334, hand_p1_);
  EXPECT_FALSE(entry_.IsFor(334, HAND_ZERO));
  EXPECT_FALSE(entry_.IsFor(264, hand_p1_));
  EXPECT_TRUE(entry_.IsFor(334, hand_p1_));
  EXPECT_EQ(entry_.GetHand(), hand_p1_);
}

TEST_F(EntryTest, MayRepeat) {
  Init(334, hand_p1_);
  entry_.Update(264, 100, 200, kMaxMateLen, 1);

  EXPECT_FALSE(entry_.MayRepeat());
  LookUp(hand_p1_, 264, kMaxMateLen, true, 100, 200, kMaxMateLen, __LINE__);

  entry_.SetRepeat();
  EXPECT_TRUE(entry_.MayRepeat());
  LookUp(hand_p1_, 264, kMaxMateLen, true, 1, 1, kMaxMateLen, __LINE__);
}

TEST_F(EntryTest, Null) {
  EXPECT_TRUE(entry_.IsNull());

  Init(334, hand_p1_);
  EXPECT_FALSE(entry_.IsNull());

  entry_.SetNull();
  EXPECT_TRUE(entry_.IsNull());
}

TEST_F(EntryTest, LookUp_Empty) {
  Init(334, hand_p1_);

  // inferior
  LookUp(HAND_ZERO, 264, kMaxMateLen, false, 1, 1, kMaxMateLen, __LINE__);
  // equal
  LookUp(hand_p1_, 264, kMaxMateLen, false, 1, 1, kMaxMateLen, __LINE__);
  // superior
  LookUp(hand_p2_, 264, kMaxMateLen, false, 1, 1, kMaxMateLen, __LINE__);
}

TEST_F(EntryTest, LookUp_Exact) {
  Init(334, hand_p1_);
  entry_.Update(264, 100, 200, kMaxMateLen, 1);

  LookUp(hand_p1_, 264, kMaxMateLen, true, 100, 200, kMaxMateLen, __LINE__);
}

TEST_F(EntryTest, LookUp_DnUpdate) {
  Init(334, hand_p1_);
  entry_.Update(264, 100, 200, {264, 3}, 1);

  // hand is superior
  LookUp(hand_p2_, 264, {264, 3}, false, 1, 200, {264, 3}, __LINE__);
  // hand is superior, and min depth is deeper
  LookUp(hand_p2_, 266, {264, 3}, false, 1, 1, {264, 3}, __LINE__);
  // hand is superior, and min depth is shallower
  LookUp(hand_p2_, 262, {264, 3}, false, 1, 200, {264, 3}, __LINE__);

  // mate_len is superior (more difficult to show it is disproven)
  LookUp(hand_p1_, 264, {266, 3}, false, 1, 200, {266, 3}, __LINE__);
  // mate_len is superior, and min depth is deeper
  LookUp(hand_p1_, 266, {266, 3}, false, 1, 200, {266, 3}, __LINE__);
}

TEST_F(EntryTest, LookUp_PnUpdate) {
  Init(334, hand_p1_);
  entry_.Update(264, 100, 200, {264, 3}, 1);

  // hand is inferior
  LookUp(HAND_ZERO, 264, {264, 3}, false, 100, 1, {264, 3}, __LINE__);
  // hand is inferior, and min depth is deeper
  LookUp(HAND_ZERO, 266, {264, 3}, false, 1, 1, {264, 3}, __LINE__);
  // hand is inferior, and min depth is shallower
  LookUp(HAND_ZERO, 262, {264, 3}, false, 100, 1, {264, 3}, __LINE__);

  // mate_len is inferior
  LookUp(hand_p1_, 264, {262, 3}, false, 100, 1, {262, 3}, __LINE__);
  // mate_len is inferior, and min depth is deeper
  LookUp(hand_p1_, 266, {262, 3}, false, 100, 1, {262, 3}, __LINE__);
}

TEST_F(EntryTest, LookUp_SuperiorProven) {
  Init(334, hand_p1_);
  entry_.Update(264, 0, kInfinitePnDn, {264, 3}, 1);

  // exact
  LookUp(hand_p1_, 264, {264, 3}, true, 0, kInfinitePnDn, {264, 3}, __LINE__);

  // hand is superior
  LookUp(hand_p2_, 264, {264, 3}, true, 0, kInfinitePnDn, {264, 3}, __LINE__);
  // hand is inferior
  LookUp(HAND_ZERO, 264, {264, 3}, false, 1, 1, {264, 3}, __LINE__);

  // mate_len is superior
  LookUp(hand_p1_, 264, {266, 3}, true, 0, kInfinitePnDn, {264, 3}, __LINE__);
  // mate_len is inferior
  LookUp(hand_p1_, 264, {262, 3}, false, 1, 1, {262, 3}, __LINE__);

  // min depth is deeper
  LookUp(hand_p1_, 266, {264, 3}, true, 0, kInfinitePnDn, {264, 3}, __LINE__);
  // min depth is shallower
  LookUp(hand_p1_, 262, {264, 3}, true, 0, kInfinitePnDn, {264, 3}, __LINE__);
}

TEST_F(EntryTest, LookUp_InferiorDisproven) {
  Init(334, hand_p1_);
  entry_.Update(264, kInfinitePnDn, 0, {264, 3}, 1);

  // exact
  LookUp(hand_p1_, 264, {264, 3}, true, kInfinitePnDn, 0, {264, 3}, __LINE__);

  // hand is inferior
  LookUp(HAND_ZERO, 264, {264, 3}, true, kInfinitePnDn, 0, {264, 3}, __LINE__);
  // hand is superior
  LookUp(hand_p2_, 264, {264, 3}, false, 1, 1, {264, 3}, __LINE__);

  // mate_len is inferior
  LookUp(hand_p1_, 264, {262, 3}, true, kInfinitePnDn, 0, {264, 3}, __LINE__);
  // mate_len is superior
  LookUp(hand_p1_, 264, {266, 3}, false, 1, 1, {266, 3}, __LINE__);

  // min depth is deeper
  LookUp(hand_p1_, 266, {264, 3}, true, kInfinitePnDn, 0, {264, 3}, __LINE__);
  // min depth is shallower
  LookUp(hand_p1_, 262, {264, 3}, true, kInfinitePnDn, 0, {264, 3}, __LINE__);
}

TEST_F(EntryTest, Parent) {
  Init(334, hand_p1_);

  entry_.UpdateParent(264, hand_p2_, 445);
  const auto [key, hand] = entry_.GetParent();
  EXPECT_EQ(key, 264);
  EXPECT_EQ(hand, hand_p2_);
  EXPECT_EQ(entry_.GetSecret(), 445);
}

TEST_F(EntryTest, ProvenClear) {
  Init(334, hand_p1_);

  // unknown entry
  entry_.Update(264, 100, 200, {264, 3}, 1);
  LookUp(hand_p1_, 264, {264, 3}, true, 100, 200, {264, 3}, __LINE__);

  // clear unknown entry
  entry_.Clear<true>(HAND_ZERO, {264, 3});
  LookUp(hand_p1_, 264, {264, 3}, false, 1, 1, {264, 3}, __LINE__);

  // proven entry
  entry_.Update(264, 0, kInfinitePnDn, {264, 3}, 1);
  LookUp(hand_p1_, 264, {264, 3}, true, 0, kInfinitePnDn, {264, 3}, __LINE__);

  // don't clear exact proven entry
  entry_.Clear<true>(hand_p1_, {264, 3});
  LookUp(hand_p1_, 264, {264, 3}, true, 0, kInfinitePnDn, {264, 3}, __LINE__);

  // don't clear unrelated entry
  entry_.Clear<true>(hand_p1_, {266, 3});
  LookUp(hand_p1_, 264, {264, 3}, true, 0, kInfinitePnDn, {264, 3}, __LINE__);

  // clear proven entry
  entry_.Clear<true>(HAND_ZERO, {264, 3});
  LookUp(hand_p1_, 264, {264, 3}, false, 1, 1, {264, 3}, __LINE__);
}

TEST_F(EntryTest, DisprovenClear) {
  Init(334, hand_p1_);
  entry_.Update(264, kInfinitePnDn, 0, {264, 3}, 1);

  // clear unknown entry
  entry_.Clear<false>(hand_p2_, {264, 3});
  LookUp(hand_p1_, 264, {264, 3}, false, 1, 1, {264, 3}, __LINE__);

  // disproven entry
  entry_.Update(264, kInfinitePnDn, 0, {264, 3}, 1);
  LookUp(hand_p1_, 264, {264, 3}, true, kInfinitePnDn, 0, {264, 3}, __LINE__);

  // don't clear exact disproven entry
  entry_.Clear<false>(hand_p1_, {264, 3});
  LookUp(hand_p1_, 264, {264, 3}, true, kInfinitePnDn, 0, {264, 3}, __LINE__);

  // don't clear unrelated entry
  entry_.Clear<false>(hand_p1_, {266, 3});
  LookUp(hand_p1_, 264, {264, 3}, true, kInfinitePnDn, 0, {264, 3}, __LINE__);

  // clear proven entry
  entry_.Clear<false>(hand_p2_, {264, 3});
  LookUp(hand_p1_, 264, {264, 3}, false, 1, 1, {264, 3}, __LINE__);
}

TEST_F(EntryTest, MinDepth) {
  Init(334, hand_p1_);

  entry_.Update(334, 100, 200, {264, 3}, 3);
  EXPECT_EQ(entry_.MinDepth(), 334);
  entry_.Update(264, 100, 200, {264, 3}, 3);
  EXPECT_EQ(entry_.MinDepth(), 264);
  entry_.Update(334, 100, 200, {264, 3}, 3);
  EXPECT_EQ(entry_.MinDepth(), 264);
}

TEST_F(EntryTest, TotalAmount) {
  Init(334, hand_p1_);
  entry_.Update(264, 100, 200, {264, 3}, 3);
  entry_.Update(264, 50, 10, {268, 3}, 3);
  entry_.Update(264, 10, 5, {270, 3}, 4);

  EXPECT_EQ(entry_.TotalAmount(), 10);
}

TEST(RepetitionTable, Clear) {
  komori::tt::detail::RepetitionTable rep_table;

  rep_table.Insert(334);
  EXPECT_EQ(rep_table.Size(), 1);
  rep_table.Clear();
  EXPECT_EQ(rep_table.Size(), 0);
}

TEST(RepetitionTable, MaxSize) {
  komori::tt::detail::RepetitionTable rep_table;

  rep_table.SetTableSizeMax(2);
  rep_table.Insert(334);
  rep_table.Insert(264);
  rep_table.Insert(445);
  EXPECT_LE(rep_table.Size(), 2);
}

TEST(RepetitionTable, Insert) {
  komori::tt::detail::RepetitionTable rep_table;

  EXPECT_FALSE(rep_table.Contains(334));
  rep_table.Insert(334);
  EXPECT_TRUE(rep_table.Contains(334));
}

namespace {

class QueryTest : public ::testing::Test {
 protected:
  void SetUp() {
    hand_p1_ = HAND_ZERO;
    add_hand(hand_p1_, PAWN);

    hand_p2_ = HAND_ZERO;
    add_hand(hand_p2_, PAWN, 2);

    tt_.Resize(1);
    query_ = tt_.BuildQueryByKey(334, hand_p1_);
  }

  static void ExpectEq(const komori::tt::SearchResult& lhs, const komori::tt::SearchResult& rhs, int line) {
    EXPECT_EQ(lhs.pn, rhs.pn) << "LINE: " << line;
    EXPECT_EQ(lhs.dn, rhs.dn) << "LINE: " << line;
    EXPECT_EQ(lhs.hand, rhs.hand) << "LINE: " << line;
    EXPECT_EQ(lhs.len, rhs.len) << "LINE: " << line;
    EXPECT_EQ(lhs.is_repetition, rhs.is_repetition) << "LINE: " << line;
    EXPECT_EQ(lhs.is_first_visit, rhs.is_first_visit) << "LINE: " << line;
    EXPECT_EQ(lhs.parent_board_key, rhs.parent_board_key) << "LINE: " << line;
    EXPECT_EQ(lhs.parent_hand, rhs.parent_hand) << "LINE: " << line;
    EXPECT_EQ(lhs.secret, rhs.secret) << "LINE: " << line;
  }

  Hand hand_p1_;
  Hand hand_p2_;
  komori::tt::TranspositionTable tt_;
  komori::tt::Query query_;
};
}  // namespace

TEST_F(QueryTest, Empty) {
  const auto res = query_.LookUp(kMaxMateLen, false);
  ExpectEq(res, {1, 1, hand_p1_, kMaxMateLen, false, true, 1, kMaxNumMateMoves, kNullKey, kNullHand, 0}, __LINE__);
}

TEST_F(QueryTest, EmptyWithInitFunc) {
  const auto res = query_.LookUp(kMaxMateLen, false, []() { return std::make_pair(PnDn{33}, PnDn{4}); });
  ExpectEq(res, {33, 4, hand_p1_, kMaxMateLen, false, true, 1, kMaxNumMateMoves, kNullKey, kNullHand, 0}, __LINE__);
}

TEST_F(QueryTest, EmptyCreate) {
  query_.LookUp(kMaxMateLen, true, []() { return std::make_pair(PnDn{33}, PnDn{4}); });
  const auto res = query_.LookUp(kMaxMateLen, false);
  ExpectEq(res, {33, 4, hand_p1_, kMaxMateLen, false, false, 1, kMaxNumMateMoves, kNullKey, kNullHand, 0}, __LINE__);
}

TEST_F(QueryTest, CreateUnknown) {
  query_.SetResult({33, 4, hand_p1_, {26, 4}, false, false, 1, kMaxNumMateMoves, 264, hand_p2_, 445});
  const auto res = query_.LookUp({26, 4}, false);
  ExpectEq(res, {33, 4, hand_p1_, {26, 4}, false, false, 1, kMaxNumMateMoves, 264, hand_p2_, 445}, __LINE__);
}

TEST_F(QueryTest, CreateRepetition) {
  query_.SetResult({33, 4, hand_p1_, {26, 4}});
  query_.SetResult({kInfinitePnDn, 0, hand_p1_, {26, 4}, true});
  const auto res = query_.LookUp({26, 4}, false);
  ExpectEq(res, {kInfinitePnDn, 0, hand_p1_, {26, 4}, true, false, 1, kMaxNumMateMoves, kNullKey, kNullHand, 0},
           __LINE__);
}

TEST_F(QueryTest, CreateProven) {
  query_.SetResult({0, kInfinitePnDn, HAND_ZERO, {22, 4}});
  const auto res = query_.LookUp({26, 4}, false);
  ExpectEq(res, {0, kInfinitePnDn, HAND_ZERO, {22, 4}, false, false, 1, kMaxNumMateMoves, kNullKey, kNullHand, 0},
           __LINE__);
}

TEST_F(QueryTest, CreateDisproven) {
  query_.SetResult({kInfinitePnDn, 0, hand_p2_, {28, 4}});
  const auto res = query_.LookUp({26, 4}, false);
  ExpectEq(res, {kInfinitePnDn, 0, hand_p2_, {28, 4}, false, false, 1, kMaxNumMateMoves, kNullKey, kNullHand, 0},
           __LINE__);
}
