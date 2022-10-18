#include <gtest/gtest.h>

#include "../tt.hpp"
#include "test_lib.hpp"

using komori::kDepthMax;
using komori::kInfinitePnDn;
using komori::kMaxMateLen;
using komori::kMaxMateLen16;
using komori::kNullHand;
using komori::kNullKey;
using komori::MateLen;
using komori::MateLen16;
using komori::PnDn;

namespace {
class EntryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    hand_p1_ = MakeHand<PAWN>();
    hand_p2_ = MakeHand<PAWN, PAWN>();
  }

  void Init(Key board_key, Hand hand) { entry_.Init(board_key, hand); }

  void LookUp(Hand hand,
              Depth depth,
              MateLen16 len,
              bool expected_res,
              PnDn expected_pn,
              PnDn expected_dn,
              MateLen16 expected_len,
              int line) {
    PnDn pn = 1;
    PnDn dn = 1;
    bool use_old_child = false;
    const auto res = entry_.LookUp(hand, depth, len, pn, dn, use_old_child);

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
  entry_.Update(264, 100, 200, kMaxMateLen16, 1);

  EXPECT_FALSE(entry_.MayRepeat());
  LookUp(hand_p1_, 264, kMaxMateLen16, true, 100, 200, kMaxMateLen16, __LINE__);

  entry_.SetRepeat();
  EXPECT_TRUE(entry_.MayRepeat());
  LookUp(hand_p1_, 264, kMaxMateLen16, true, 1, 1, kMaxMateLen16, __LINE__);
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
  LookUp(HAND_ZERO, 264, kMaxMateLen16, false, 1, 1, kMaxMateLen16, __LINE__);
  // equal
  LookUp(hand_p1_, 264, kMaxMateLen16, false, 1, 1, kMaxMateLen16, __LINE__);
  // superior
  LookUp(hand_p2_, 264, kMaxMateLen16, false, 1, 1, kMaxMateLen16, __LINE__);
}

TEST_F(EntryTest, LookUp_Exact) {
  Init(334, hand_p1_);
  entry_.Update(264, 100, 200, kMaxMateLen16, 1);

  LookUp(hand_p1_, 264, kMaxMateLen16, true, 100, 200, kMaxMateLen16, __LINE__);
}

TEST_F(EntryTest, LookUp_NotFound) {
  Init(334, hand_p1_);
  entry_.Update(264, 100, 200, MateLen16::Make(33, 4), 1);
  entry_.Update(264, 101, 201, MateLen16::Make(35, 4), 1);
  entry_.Update(264, 102, 202, MateLen16::Make(37, 4), 1);
  entry_.Update(264, 103, 203, MateLen16::Make(39, 4), 1);
  entry_.Update(264, 105, 205, MateLen16::Make(41, 4), 1);
  entry_.Update(264, 106, 206, MateLen16::Make(43, 4), 1);

  LookUp(hand_p1_, 264, MateLen16::Make(45, 4), false, 1, 206, MateLen16::Make(45, 4), __LINE__);
}

TEST_F(EntryTest, LookUp_DnUpdate) {
  Init(334, hand_p1_);
  entry_.Update(264, 100, 200, MateLen16::Make(264, 3), 1);

  // hand is superior
  LookUp(hand_p2_, 264, MateLen16::Make(264, 3), false, 1, 200, MateLen16::Make(264, 3), __LINE__);
  // hand is superior, and min depth is deeper
  LookUp(hand_p2_, 266, MateLen16::Make(264, 3), false, 1, 1, MateLen16::Make(264, 3), __LINE__);
  // hand is superior, and min depth is shallower
  LookUp(hand_p2_, 262, MateLen16::Make(264, 3), false, 1, 200, MateLen16::Make(264, 3), __LINE__);

  // mate_len is superior (more difficult to show it is disproven)
  LookUp(hand_p1_, 264, MateLen16::Make(266, 3), false, 1, 200, MateLen16::Make(266, 3), __LINE__);
  // mate_len is superior, and min depth is deeper
  LookUp(hand_p1_, 266, MateLen16::Make(266, 3), false, 1, 200, MateLen16::Make(266, 3), __LINE__);
}

TEST_F(EntryTest, LookUp_PnUpdate) {
  Init(334, hand_p1_);
  entry_.Update(264, 100, 200, MateLen16::Make(264, 3), 1);

  // hand is inferior
  LookUp(HAND_ZERO, 264, MateLen16::Make(264, 3), false, 100, 1, MateLen16::Make(264, 3), __LINE__);
  // hand is inferior, and min depth is deeper
  LookUp(HAND_ZERO, 266, MateLen16::Make(264, 3), false, 1, 1, MateLen16::Make(264, 3), __LINE__);
  // hand is inferior, and min depth is shallower
  LookUp(HAND_ZERO, 262, MateLen16::Make(264, 3), false, 100, 1, MateLen16::Make(264, 3), __LINE__);

  // mate_len is inferior
  LookUp(hand_p1_, 264, MateLen16::Make(262, 3), false, 100, 1, MateLen16::Make(262, 3), __LINE__);
  // mate_len is inferior, and min depth is deeper
  LookUp(hand_p1_, 266, MateLen16::Make(262, 3), false, 100, 1, MateLen16::Make(262, 3), __LINE__);
}

TEST_F(EntryTest, LookUp_SuperiorProven) {
  Init(334, hand_p1_);
  entry_.Update(264, 0, kInfinitePnDn, MateLen16::Make(264, 3), 1);

  // exact
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), true, 0, kInfinitePnDn, MateLen16::Make(264, 3), __LINE__);

  // hand is superior
  LookUp(hand_p2_, 264, MateLen16::Make(264, 3), true, 0, kInfinitePnDn, MateLen16::Make(264, 3), __LINE__);
  // hand is inferior
  LookUp(HAND_ZERO, 264, MateLen16::Make(264, 3), false, 1, 1, MateLen16::Make(264, 3), __LINE__);

  // mate_len is superior
  LookUp(hand_p1_, 264, MateLen16::Make(266, 3), true, 0, kInfinitePnDn, MateLen16::Make(264, 3), __LINE__);
  // mate_len is inferior
  LookUp(hand_p1_, 264, MateLen16::Make(262, 3), false, 1, 1, MateLen16::Make(262, 3), __LINE__);

  // min depth is deeper
  LookUp(hand_p1_, 266, MateLen16::Make(264, 3), true, 0, kInfinitePnDn, MateLen16::Make(264, 3), __LINE__);
  // min depth is shallower
  LookUp(hand_p1_, 262, MateLen16::Make(264, 3), true, 0, kInfinitePnDn, MateLen16::Make(264, 3), __LINE__);
}

TEST_F(EntryTest, LookUp_InferiorDisproven) {
  Init(334, hand_p1_);
  entry_.Update(264, kInfinitePnDn, 0, MateLen16::Make(264, 3), 1);

  // exact
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), true, kInfinitePnDn, 0, MateLen16::Make(264, 3), __LINE__);

  // hand is inferior
  LookUp(HAND_ZERO, 264, MateLen16::Make(264, 3), true, kInfinitePnDn, 0, MateLen16::Make(264, 3), __LINE__);
  // hand is superior
  LookUp(hand_p2_, 264, MateLen16::Make(264, 3), false, 1, 1, MateLen16::Make(264, 3), __LINE__);

  // mate_len is inferior
  LookUp(hand_p1_, 264, MateLen16::Make(262, 3), true, kInfinitePnDn, 0, MateLen16::Make(264, 3), __LINE__);
  // mate_len is superior
  LookUp(hand_p1_, 264, MateLen16::Make(266, 3), false, 1, 1, MateLen16::Make(266, 3), __LINE__);

  // min depth is deeper
  LookUp(hand_p1_, 266, MateLen16::Make(264, 3), true, kInfinitePnDn, 0, MateLen16::Make(264, 3), __LINE__);
  // min depth is shallower
  LookUp(hand_p1_, 262, MateLen16::Make(264, 3), true, kInfinitePnDn, 0, MateLen16::Make(264, 3), __LINE__);
}

TEST_F(EntryTest, Update_ProvenSkip) {
  Init(334, hand_p1_);
  entry_.Update(264, 0, kInfinitePnDn, MateLen16::Make(264, 3), 1);
  entry_.Update(264, 0, kInfinitePnDn, MateLen16::Make(266, 3), 1);

  LookUp(hand_p1_, 264, MateLen16::Make(266, 3), true, 0, kInfinitePnDn, MateLen16::Make(264, 3), __LINE__);
}

TEST_F(EntryTest, Update_DisprovenSkip) {
  Init(334, hand_p1_);
  entry_.Update(264, kInfinitePnDn, 0, MateLen16::Make(264, 3), 1);
  entry_.Update(264, kInfinitePnDn, 0, MateLen16::Make(262, 3), 1);

  LookUp(hand_p1_, 264, MateLen16::Make(262, 3), true, kInfinitePnDn, 0, MateLen16::Make(264, 3), __LINE__);
}

TEST_F(EntryTest, Update_Overwrite) {
  Init(334, hand_p1_);
  entry_.Update(264, 1, 7, MateLen16::Make(264, 3), 1);
  entry_.Update(264, 2, 6, MateLen16::Make(264, 4), 1);
  entry_.Update(264, 3, 5, MateLen16::Make(264, 5), 1);
  entry_.Update(264, 4, 4, MateLen16::Make(264, 6), 1);
  entry_.Update(264, 5, 3, MateLen16::Make(264, 7), 1);
  entry_.Update(264, 6, 2, MateLen16::Make(264, 8), 1);
  entry_.Update(264, 7, 1, MateLen16::Make(264, 9), 1);

  LookUp(hand_p1_, 264, MateLen16::Make(264, 9), true, 7, 1, MateLen16::Make(264, 9), __LINE__);
}

TEST_F(EntryTest, Parent) {
  Init(334, hand_p1_);

  entry_.UpdateParent(264, hand_p2_, 445);
  const auto [key, hand] = entry_.GetParent();
  EXPECT_EQ(key, 264);
  EXPECT_EQ(hand, hand_p2_);
  EXPECT_EQ(entry_.GetSecret(), 445);
}

TEST_F(EntryTest, Clear_Proven) {
  Init(334, hand_p1_);

  // unknown entry
  entry_.Update(264, 100, 200, MateLen16::Make(264, 3), 1);
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), true, 100, 200, MateLen16::Make(264, 3), __LINE__);

  // clear unknown entry
  entry_.Clear<true>(HAND_ZERO, MateLen16::Make(262, 3));
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), false, 1, 1, MateLen16::Make(264, 3), __LINE__);

  // proven entry
  entry_.Update(264, 0, kInfinitePnDn, MateLen16::Make(264, 3), 1);
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), true, 0, kInfinitePnDn, MateLen16::Make(264, 3), __LINE__);

  // don't clear exact proven entry
  entry_.Clear<true>(hand_p1_, MateLen16::Make(264, 3));
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), true, 0, kInfinitePnDn, MateLen16::Make(264, 3), __LINE__);

  // don't clear unrelated entry
  entry_.Clear<true>(hand_p1_, MateLen16::Make(266, 3));
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), true, 0, kInfinitePnDn, MateLen16::Make(264, 3), __LINE__);

  // clear proven entry
  entry_.Clear<true>(HAND_ZERO, MateLen16::Make(264, 3));
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), false, 1, 1, MateLen16::Make(264, 3), __LINE__);
}

TEST_F(EntryTest, Clear_Disproven) {
  Init(334, hand_p1_);
  entry_.Update(264, kInfinitePnDn, 0, MateLen16::Make(264, 3), 1);

  // clear unknown entry
  entry_.Clear<false>(hand_p2_, MateLen16::Make(266, 3));
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), false, 1, 1, MateLen16::Make(264, 3), __LINE__);

  // disproven entry
  entry_.Update(264, kInfinitePnDn, 0, MateLen16::Make(264, 3), 1);
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), true, kInfinitePnDn, 0, MateLen16::Make(264, 3), __LINE__);

  // don't clear exact disproven entry
  entry_.Clear<false>(hand_p1_, MateLen16::Make(264, 3));
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), true, kInfinitePnDn, 0, MateLen16::Make(264, 3), __LINE__);

  // don't clear unrelated entry
  entry_.Clear<false>(hand_p1_, MateLen16::Make(262, 3));
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), true, kInfinitePnDn, 0, MateLen16::Make(264, 3), __LINE__);

  // clear proven entry
  entry_.Clear<false>(hand_p2_, MateLen16::Make(264, 3));
  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), false, 1, 1, MateLen16::Make(264, 3), __LINE__);
}

TEST_F(EntryTest, Clear_Compaction) {
  Init(334, hand_p1_);
  entry_.Update(264, 10, 20, MateLen16::Make(266, 3), 1);
  entry_.Update(264, 0, kInfinitePnDn, MateLen16::Make(264, 3), 1);
  entry_.Clear<true>(hand_p1_, MateLen16::Make(264, 3));

  LookUp(hand_p1_, 264, MateLen16::Make(264, 3), true, 0, kInfinitePnDn, MateLen16::Make(264, 3), __LINE__);
}

TEST_F(EntryTest, MinDepth) {
  Init(334, hand_p1_);

  entry_.Update(334, 100, 200, MateLen16::Make(264, 3), 3);
  EXPECT_EQ(entry_.MinDepth(), 334);
  entry_.Update(264, 100, 200, MateLen16::Make(264, 3), 3);
  EXPECT_EQ(entry_.MinDepth(), 264);
  entry_.Update(334, 100, 200, MateLen16::Make(264, 3), 3);
  EXPECT_EQ(entry_.MinDepth(), 264);
}

TEST_F(EntryTest, TotalAmount) {
  Init(334, hand_p1_);
  entry_.Update(264, 100, 200, MateLen16::Make(264, 3), 3);
  entry_.Update(264, 50, 10, MateLen16::Make(268, 3), 3);
  entry_.Update(264, 10, 5, MateLen16::Make(270, 3), 4);

  EXPECT_EQ(entry_.TotalAmount(), 10);
}

namespace {

class QueryTest : public ::testing::Test {
 protected:
  void SetUp() {
    hand_p1_ = MakeHand<PAWN>();
    hand_p2_ = MakeHand<PAWN, PAWN>();

    tt_.Resize(1);
    query_ = tt_.BuildQueryByKey(334, hand_p1_);
  }

  static void ExpectBase(const komori::SearchResult& result,
                         PnDn expected_pn,
                         PnDn expected_dn,
                         Hand expected_hand,
                         MateLen expected_len,
                         std::uint32_t expected_amount,
                         int line) {
    EXPECT_EQ(result.Pn(), expected_pn) << "LINE: " << line;
    EXPECT_EQ(result.Dn(), expected_dn) << "LINE: " << line;
    EXPECT_EQ(result.GetHand(), expected_hand) << "LINE: " << line;
    EXPECT_EQ(result.Len(), expected_len) << "LINE: " << line;
    EXPECT_EQ(result.Amount(), expected_amount) << "LINE: " << line;
  }

  static void ExpectUnknown(const komori::SearchResult& result,
                            bool expected_is_first_visit,
                            Key expected_parent_board_key,
                            Hand expected_parent_hand,
                            std::uint64_t expected_secret,
                            int line) {
    const auto& unknown_data = result.GetUnknownData();
    EXPECT_EQ(unknown_data.is_first_visit, expected_is_first_visit) << "LINE: " << line;
    EXPECT_EQ(unknown_data.parent_board_key, expected_parent_board_key) << "LINE: " << line;
    EXPECT_EQ(unknown_data.parent_hand, expected_parent_hand) << "LINE: " << line;
    EXPECT_EQ(unknown_data.secret, expected_secret) << "LINE: " << line;
  }

  static void ExpectFinal(const komori::SearchResult& result, bool expected_is_repetition, int line) {
    EXPECT_EQ(result.GetFinalData().is_repetition, expected_is_repetition) << "LINE: " << line;
  }

  Hand hand_p1_;
  Hand hand_p2_;
  komori::tt::TranspositionTable tt_;
  komori::tt::Query query_;
};
}  // namespace

TEST_F(QueryTest, Empty) {
  const auto res = query_.LookUp(kMaxMateLen, false);
  ExpectBase(res, 1, 1, hand_p1_, kMaxMateLen, 1, __LINE__);
  ExpectUnknown(res, true, kNullKey, kNullHand, 0, __LINE__);
}

TEST_F(QueryTest, EmptyWithInitFunc) {
  const auto res = query_.LookUp(kMaxMateLen, false, []() { return std::make_pair(PnDn{33}, PnDn{4}); });
  ExpectBase(res, 33, 4, hand_p1_, kMaxMateLen, 1, __LINE__);
  ExpectUnknown(res, true, kNullKey, kNullHand, 0, __LINE__);
}

TEST_F(QueryTest, EmptyCreate) {
  query_.LookUp(kMaxMateLen, true, []() { return std::make_pair(PnDn{33}, PnDn{4}); });
  const auto res = query_.LookUp(kMaxMateLen, false);
  ExpectBase(res, 33, 4, hand_p1_, kMaxMateLen, 1, __LINE__);
  ExpectUnknown(res, false, kNullKey, kNullHand, 0, __LINE__);
}

TEST_F(QueryTest, CreateUnknown) {
  komori::UnknownData unknown_data = {false, 264, hand_p2_, 445};
  const auto set_result = komori::SearchResult::MakeUnknown(33, 4, hand_p1_, MateLen::Make(26, 4), 1, unknown_data);

  query_.SetResult(set_result);
  const auto res = query_.LookUp(MateLen::Make(26, 4), false);
  ExpectBase(res, 33, 4, hand_p1_, MateLen::Make(26, 4), 1, __LINE__);
  ExpectUnknown(res, false, 264, hand_p2_, 445, __LINE__);
}

TEST_F(QueryTest, CreateRepetition) {
  query_.SetResult(komori::SearchResult::MakeFinal<false, true>(hand_p1_, MateLen::Make(26, 4), 1));
  const auto res_1 = query_.LookUp(MateLen::Make(26, 4), false);
  ExpectBase(res_1, 1, 1, hand_p1_, MateLen::Make(26, 4), 1, __LINE__);

  komori::UnknownData unknown_data = {false, 264, hand_p1_, 445};
  query_.SetResult(komori::SearchResult::MakeUnknown(33, 4, hand_p1_, MateLen::Make(26, 4), 1, unknown_data));

  query_.SetResult(komori::SearchResult::MakeFinal<false, true>(hand_p1_, MateLen::Make(26, 4), 1));
  const auto res_2 = query_.LookUp(MateLen::Make(26, 4), false);

  ExpectBase(res_2, kInfinitePnDn, 0, hand_p1_, MateLen::Make(26, 4), 1, __LINE__);
  ExpectFinal(res_2, true, __LINE__);
}

TEST_F(QueryTest, CreateProven) {
  const auto proven_result = komori::SearchResult::MakeFinal<true>(HAND_ZERO, MateLen::Make(22, 4), 10);

  query_.SetResult(proven_result);
  const auto res = query_.LookUp(MateLen::Make(26, 4), false);

  ExpectBase(res, 0, kInfinitePnDn, HAND_ZERO, MateLen::Make(22, 4), 10, __LINE__);
  ExpectFinal(res, false, __LINE__);
}

TEST_F(QueryTest, CreateDisproven) {
  const auto disproven_result = komori::SearchResult::MakeFinal<false>(hand_p2_, MateLen::Make(28, 4), 10);

  query_.SetResult(disproven_result);
  const auto res = query_.LookUp(MateLen::Make(26, 4), false);
  ExpectBase(res, kInfinitePnDn, 0, hand_p2_, MateLen::Make(28, 4), 10, __LINE__);
  ExpectFinal(res, false, __LINE__);
}

TEST_F(QueryTest, CreateDoubleUnknown) {
  komori::UnknownData unknown_data_1 = {false, 264, hand_p2_, 445};
  const auto set_result_1 = komori::SearchResult::MakeUnknown(33, 4, hand_p2_, MateLen::Make(26, 4), 1, unknown_data_1);

  query_.SetResult(set_result_1);

  komori::UnknownData unknown_data_2 = {false, 334, HAND_ZERO, 4450};
  const auto set_result_2 =
      komori::SearchResult::MakeUnknown(330, 40, hand_p1_, MateLen::Make(26, 4), 1, unknown_data_2);
  query_.SetResult(set_result_2);
  const auto res = query_.LookUp(MateLen::Make(26, 4), false);
  ExpectBase(res, 330, 40, hand_p1_, MateLen::Make(26, 4), 1, __LINE__);
  ExpectUnknown(res, false, 334, HAND_ZERO, 4450, __LINE__);
}

TEST_F(QueryTest, CreateOverwriteUnknown) {
  komori::UnknownData unknown_data = {false, 264, hand_p2_, 445};
  const auto set_result_1 = komori::SearchResult::MakeUnknown(33, 4, hand_p1_, MateLen::Make(26, 4), 1, unknown_data);

  query_.SetResult(set_result_1);

  const auto set_result_2 = komori::SearchResult::MakeUnknown(330, 40, hand_p1_, MateLen::Make(26, 4), 1, unknown_data);
  query_.SetResult(set_result_2);
  const auto res = query_.LookUp(MateLen::Make(26, 4), false);
  ExpectBase(res, 330, 40, hand_p1_, MateLen::Make(26, 4), 1, __LINE__);
  ExpectUnknown(res, false, 264, hand_p2_, 445, __LINE__);
}

TEST_F(QueryTest, CreateOverflow) {
  komori::UnknownData unknown_data = {false, 264, hand_p2_, 445};
  for (std::size_t i = 0; i < komori::tt::kClusterSize; ++i) {
    auto hand = HAND_ZERO;
    add_hand(hand, LANCE, 1);
    add_hand(hand, PAWN, i);

    const auto result = komori::SearchResult::MakeUnknown(33, i + 334, hand, MateLen::Make(26, 4), 1, unknown_data);
    query_.SetResult(result);
  }

  const auto result = komori::SearchResult::MakeUnknown(33, 264, hand_p1_, MateLen::Make(26, 4), 1, unknown_data);
  query_.SetResult(result);

  const auto res = query_.LookUp(MateLen::Make(26, 4), false);
  ExpectBase(res, 33, 264, hand_p1_, MateLen::Make(26, 4), 1, __LINE__);
  ExpectUnknown(res, false, 264, hand_p2_, 445, __LINE__);
}
