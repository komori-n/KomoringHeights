#include <gtest/gtest.h>

#include <vector>

#include "../ttquery.hpp"
#include "test_lib.hpp"

using komori::BitSet64;
using komori::FinalData;
using komori::kInfinitePnDn;
using komori::kPnDnUnit;
using komori::MateLen;
using komori::MateLen16;
using komori::PnDn;
using komori::RepetitionTable;
using komori::SearchResult;
using komori::UnknownData;
using komori::tt::Cluster;
using komori::tt::Entry;
using komori::tt::Query;
using komori::tt::SearchAmount;

namespace {
class QueryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    entries_.resize(Cluster::kSize);
    rep_table_.SetTableSizeMax(334);

    query_ = Query{rep_table_, {entries_.data()}, path_key_, board_key_, hand_, depth_};
  }

  Query query_;

  std::vector<Entry> entries_;
  RepetitionTable rep_table_;

  const Key path_key_{0x264264};
  const Key board_key_{0x3304};
  const Hand hand_{MakeHand<PAWN, LANCE, LANCE>()};
  const Depth depth_{334};
};

constexpr auto kDefaultInitialEvalFunc = []() { return std::make_pair(kPnDnUnit, kPnDnUnit); };
}  // namespace

TEST_F(QueryTest, LoopUp_None) {
  bool does_have_old_child{false};
  const auto result = query_.LookUp(does_have_old_child, MateLen{334}, kDefaultInitialEvalFunc);

  EXPECT_EQ(result.Pn(), kPnDnUnit);
  EXPECT_EQ(result.Dn(), kPnDnUnit);
  EXPECT_EQ(result.GetUnknownData().sum_mask, BitSet64::Full());
}

TEST_F(QueryTest, LoopUp_UnknownExact) {
  for (std::size_t i = 0; i < Cluster::kSize; ++i) {
    const PnDn pn{33 * (i + 1)};
    const PnDn dn{4 * (i + 1)};
    const BitSet64 bs{0x334 * (i + 1)};
    const SearchAmount amount{334};

    entries_[i].Init(board_key_, hand_);
    entries_[i].UpdateUnknown(depth_, pn, dn, amount, bs, 0, HAND_ZERO);

    bool does_have_old_child{false};
    const auto result = query_.LookUp(does_have_old_child, MateLen{334}, kDefaultInitialEvalFunc);

    EXPECT_EQ(result.Pn(), pn) << i;
    EXPECT_EQ(result.Dn(), dn) << i;
    EXPECT_EQ(result.GetUnknownData().sum_mask, bs) << i;
    EXPECT_EQ(result.Amount(), entries_[i].Amount()) << i;

    entries_[i].SetNull();
  }
}

TEST_F(QueryTest, LoopUp_UnknownExactRepetition) {
  rep_table_.Insert(path_key_, depth_ - 4);

  const PnDn pn{33};
  const PnDn dn{4};

  entries_[0].Init(board_key_, hand_);
  entries_[0].SetPossibleRepetition();
  entries_[0].UpdateUnknown(depth_, pn, dn, 1, BitSet64::Full(), 0, HAND_ZERO);

  bool does_have_old_child{false};
  const auto result = query_.LookUp(does_have_old_child, MateLen{334}, kDefaultInitialEvalFunc);

  EXPECT_EQ(result.Pn(), kInfinitePnDn);
  EXPECT_EQ(result.Dn(), 0);
  EXPECT_EQ(result.Amount(), entries_[0].Amount());
  EXPECT_EQ(result.GetFinalData().repetition_start, depth_ - 4);
}

TEST_F(QueryTest, LoopUp_UnknownExactNoRepetition) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_, hand_);
  entries_[0].UpdateUnknown(depth_, pn, dn, amount, BitSet64::Full(), 0, HAND_ZERO);
  entries_[0].SetPossibleRepetition();
  entries_[0].UpdateUnknown(board_key_, pn, dn, 1, BitSet64::Full(), 0, HAND_ZERO);

  bool does_have_old_child{false};
  const auto result = query_.LookUp(does_have_old_child, MateLen{334}, kDefaultInitialEvalFunc);

  EXPECT_EQ(result.Pn(), pn);
  EXPECT_EQ(result.Dn(), dn);
  EXPECT_EQ(result.Amount(), entries_[0].Amount());
}

TEST_F(QueryTest, LoopUp_DifferentBoardKey) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_ ^ 0x01, hand_);
  entries_[0].UpdateUnknown(depth_, pn, dn, amount, BitSet64::Full(), 0, HAND_ZERO);

  bool does_have_old_child{false};
  const auto result = query_.LookUp(does_have_old_child, MateLen{334}, kDefaultInitialEvalFunc);

  EXPECT_EQ(result.Pn(), kPnDnUnit);
  EXPECT_EQ(result.Dn(), kPnDnUnit);
  EXPECT_EQ(result.Amount(), 1);
}

TEST_F(QueryTest, LoopUp_DifferentHand) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_, MakeHand<GOLD>());
  entries_[0].UpdateUnknown(depth_, pn, dn, amount, BitSet64::Full(), 0, HAND_ZERO);

  bool does_have_old_child{false};
  const auto result = query_.LookUp(does_have_old_child, MateLen{334}, kDefaultInitialEvalFunc);

  EXPECT_EQ(result.Pn(), kPnDnUnit);
  EXPECT_EQ(result.Dn(), kPnDnUnit);
  EXPECT_EQ(result.Amount(), 1);
}

TEST_F(QueryTest, LoopUp_UnknownSuperior) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_, MakeHand<PAWN>());
  entries_[0].UpdateUnknown(depth_, pn, dn, amount, BitSet64::Full(), 0, HAND_ZERO);

  bool does_have_old_child{false};
  const auto result = query_.LookUp(does_have_old_child, MateLen{334}, kDefaultInitialEvalFunc);

  EXPECT_EQ(result.Pn(), kPnDnUnit);
  EXPECT_EQ(result.Dn(), dn);
  EXPECT_EQ(result.Amount(), amount);
}

TEST_F(QueryTest, LoopUp_UnknownInferior) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_, MakeHand<PAWN, LANCE, LANCE, GOLD>());
  entries_[0].UpdateUnknown(depth_, pn, dn, amount, BitSet64::Full(), 0, HAND_ZERO);

  bool does_have_old_child{false};
  const auto result = query_.LookUp(does_have_old_child, MateLen{334}, kDefaultInitialEvalFunc);

  EXPECT_EQ(result.Pn(), pn);
  EXPECT_EQ(result.Dn(), kPnDnUnit);
  EXPECT_EQ(result.Amount(), amount);

  entries_[0].SetNull();
}

TEST_F(QueryTest, LoopUp_Proven) {
  const auto hand = MakeHand<PAWN>();
  entries_[0].Init(board_key_, hand);
  entries_[0].UpdateProven(MateLen16{264}, 1);

  bool does_have_old_child{false};
  const auto result = query_.LookUp(does_have_old_child, MateLen{334}, kDefaultInitialEvalFunc);

  EXPECT_EQ(result.Pn(), 0);
  EXPECT_EQ(result.Dn(), kInfinitePnDn);
  EXPECT_EQ(result.Len(), MateLen{264});
  EXPECT_EQ(result.Amount(), entries_[0].Amount());
  EXPECT_EQ(result.GetFinalData().hand, hand);
}

TEST_F(QueryTest, LoopUp_Disproven) {
  const auto hand = MakeHand<PAWN, LANCE, LANCE, LANCE>();
  entries_[0].Init(board_key_, hand);
  entries_[0].UpdateDisproven(MateLen16{3340}, 1);

  bool does_have_old_child{false};
  const auto result = query_.LookUp(does_have_old_child, MateLen{334}, kDefaultInitialEvalFunc);

  EXPECT_EQ(result.Pn(), kInfinitePnDn);
  EXPECT_EQ(result.Dn(), 0);
  EXPECT_EQ(result.Len(), MateLen{3340});
  EXPECT_EQ(result.Amount(), entries_[0].Amount());
  EXPECT_EQ(result.GetFinalData().hand, hand);
}

TEST_F(QueryTest, LookUpParent_Empty) {
  PnDn pn{1}, dn{1};
  const auto parent_key_hand_pair = query_.LookUpParent(pn, dn);
  EXPECT_EQ(parent_key_hand_pair, std::nullopt);
}

TEST_F(QueryTest, LookUpParent_NoData) {
  PnDn pn{1}, dn{1};
  entries_[3].Init(board_key_, hand_);
  const auto parent_key_hand_pair = query_.LookUpParent(pn, dn);
  EXPECT_EQ(parent_key_hand_pair, std::nullopt);
}

TEST_F(QueryTest, LookUpParent_Exact) {
  const PnDn ans_pn{33};
  const PnDn ans_dn{4};
  const Key board_key{0x3304};
  const Hand hand{MakeHand<PAWN, LANCE, LANCE>()};

  entries_[5].Init(board_key_, hand_);
  entries_[5].UpdateUnknown(264, ans_pn, ans_dn, 1, BitSet64::Full(), board_key, hand);
  PnDn pn{1}, dn{1};
  const auto parent_key_hand_pair = query_.LookUpParent(pn, dn);
  ASSERT_NE(parent_key_hand_pair, std::nullopt);
  EXPECT_EQ(parent_key_hand_pair->board_key, board_key);
  EXPECT_EQ(parent_key_hand_pair->hand, hand);
  EXPECT_EQ(pn, ans_pn);
  EXPECT_EQ(dn, ans_dn);
}

TEST_F(QueryTest, FinalRange_Normal) {
  const auto len1 = MateLen{334};
  const auto len2 = MateLen{264};
  entries_[0].Init(board_key_, MakeHand<PAWN>());
  entries_[0].UpdateProven(MateLen16{len1}, 1);
  entries_[1].Init(board_key_, MakeHand<PAWN, LANCE, LANCE, GOLD>());
  entries_[1].UpdateDisproven(MateLen16{len2}, 1);

  entries_[2].Init(board_key_, HAND_ZERO);
  entries_[2].SetNull();

  const auto [disproven_len, proven_len] = query_.FinalRange();
  EXPECT_EQ(disproven_len, len2);
  EXPECT_EQ(proven_len, len1);
}

TEST_F(QueryTest, FinalRange_Repetition) {
  const auto len = MateLen{334};
  entries_[0].Init(board_key_, hand_);
  entries_[0].UpdateProven(MateLen16{len}, 1);

  const auto [disproven_len1, proven_len1] = query_.FinalRange();
  EXPECT_EQ(disproven_len1, komori::kMinus1MateLen);
  EXPECT_EQ(proven_len1, len);

  entries_[0].SetPossibleRepetition();
  const auto [disproven_len2, proven_len2] = query_.FinalRange();
  EXPECT_EQ(disproven_len2, komori::kMinus1MateLen);
  EXPECT_EQ(proven_len2, len);

  rep_table_.Insert(path_key_, 264);
  const auto [disproven_len3, proven_len3] = query_.FinalRange();
  EXPECT_EQ(disproven_len3, len - 1);
  EXPECT_EQ(proven_len3, len);
}

TEST_F(QueryTest, SetResult_UnknownNew) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};
  const UnknownData unknown_data{};
  const SearchResult result = SearchResult::MakeUnknown(pn, dn, MateLen{334}, amount, unknown_data);

  query_.SetResult(result);
  EXPECT_EQ(entries_[0].Pn(), pn);
  EXPECT_EQ(entries_[0].Dn(), dn);
  EXPECT_EQ(entries_[0].Amount(), amount);
}

TEST_F(QueryTest, SetResult_UnknownUpdate) {
  for (std::size_t i = 0; i < Cluster::kSize; ++i) {
    const PnDn pn{33 * (i + 1)};
    const PnDn dn{4 * (i + 1)};
    const SearchAmount amount{static_cast<SearchAmount>(334 * (i + 1))};
    entries_[i].Init(board_key_, hand_);

    const UnknownData unknown_data{};
    const SearchResult result = SearchResult::MakeUnknown(pn, dn, MateLen{334}, amount, unknown_data);

    query_.SetResult(result);
    EXPECT_EQ(entries_[i].Pn(), pn) << i;
    EXPECT_EQ(entries_[i].Dn(), dn) << i;
    EXPECT_EQ(entries_[i].Amount(), 1 / 2 + amount) << i;

    entries_[i].SetNull();
  }
}

TEST_F(QueryTest, SetResult_UnknownOverwrite) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  for (std::size_t i = 0; i < Cluster::kSize; ++i) {
    // entries_[8] の探索量が最小になるように初期化
    entries_[i].Init(0x264, HAND_ZERO);
    entries_[i].UpdateUnknown(1, 1, 1, 1 + (8 - i) * (8 - i), BitSet64::Full(), 0, HAND_ZERO);
  }

  const UnknownData unknown_data{};
  const SearchResult result = SearchResult::MakeUnknown(pn, dn, MateLen{334}, amount, unknown_data);
  query_.SetResult(result);

  EXPECT_EQ(entries_[8].Pn(), pn);
  EXPECT_EQ(entries_[8].Dn(), dn);
  EXPECT_EQ(entries_[8].Amount(), amount);
}

TEST_F(QueryTest, SetResult_ProvenNew) {
  const auto hand = MakeHand<PAWN>();
  const MateLen len = MateLen{334};
  const SearchResult result = SearchResult::MakeFinal<true>(hand, len, 1);

  query_.SetResult(result);
  EXPECT_EQ(entries_[0].ProvenLen(), MateLen16{len});
}

TEST_F(QueryTest, SetResult_ProvenUpdate) {
  const auto hand = MakeHand<PAWN>();
  const MateLen len = MateLen{334};
  const SearchResult result = SearchResult::MakeFinal<true>(hand, len, 1);

  entries_[2].Init(board_key_, hand);
  query_.SetResult(result);
  EXPECT_EQ(entries_[2].ProvenLen(), MateLen16{len});
}

TEST_F(QueryTest, SetResult_DisprovenNew) {
  const auto hand = MakeHand<PAWN, LANCE, LANCE, GOLD>();
  const MateLen len = MateLen{334};
  const SearchResult result = SearchResult::MakeFinal<false>(hand, len, 1);

  query_.SetResult(result);
  EXPECT_EQ(entries_[0].DisprovenLen(), MateLen16{len});
}

TEST_F(QueryTest, SetResult_DisprovenUpdate) {
  const auto hand = MakeHand<PAWN, LANCE, LANCE, GOLD>();
  const MateLen len = MateLen{334};
  const SearchResult result = SearchResult::MakeFinal<false>(hand, len, 1);

  entries_[2].Init(board_key_, hand);
  query_.SetResult(result);
  EXPECT_EQ(entries_[2].DisprovenLen(), MateLen16{len});
}

TEST_F(QueryTest, SetResult_RepetitionNew) {
  const SearchAmount amount{334};
  const SearchResult result = SearchResult::MakeRepetition(hand_, MateLen{334}, amount, 0);

  query_.SetResult(result);
  EXPECT_EQ(entries_[0].Pn(), 1);
  EXPECT_EQ(entries_[0].Dn(), 1);
  EXPECT_EQ(entries_[0].Amount(), 1);
  EXPECT_TRUE(rep_table_.Contains(path_key_));
}

TEST_F(QueryTest, SetResult_RepetitionUpdate) {
  const SearchAmount amount{334};
  const SearchResult result = SearchResult::MakeRepetition(hand_, MateLen{334}, amount, 0);

  entries_[2].Init(board_key_, hand_);
  query_.SetResult(result);
  EXPECT_EQ(entries_[2].Pn(), 1);
  EXPECT_EQ(entries_[2].Dn(), 1);
  EXPECT_EQ(entries_[2].Amount(), 1);
  EXPECT_TRUE(rep_table_.Contains(path_key_));
}
