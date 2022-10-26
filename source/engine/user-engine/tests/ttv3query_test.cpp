#include <gtest/gtest.h>

#include <vector>

#include "../ttv3query.hpp"
#include "test_lib.hpp"

using komori::FinalData;
using komori::kInfinitePnDn;
using komori::kPnDnUnit;
using komori::MateLen;
using komori::MateLen16;
using komori::PnDn;
using komori::RepetitionTable;
using komori::SearchResult;
using komori::UnknownData;
using komori::ttv3::Cluster;
using komori::ttv3::Entry;
using komori::ttv3::Query;
using komori::ttv3::SearchAmount;

namespace {
class V3QueryTest : public ::testing::Test {
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
}  // namespace

TEST_F(V3QueryTest, LoopUp_None) {
  bool does_have_old_child{false};
  const auto result = query_.LookUp<false>(does_have_old_child, MateLen::Make(33, 4));

  EXPECT_EQ(result.Pn(), kPnDnUnit);
  EXPECT_EQ(result.Dn(), kPnDnUnit);
}

TEST_F(V3QueryTest, LoopUp_UnknownExact) {
  for (std::size_t i = 0; i < Cluster::kSize; ++i) {
    const PnDn pn{33 * (i + 1)};
    const PnDn dn{4 * (i + 1)};
    const SearchAmount amount{334};

    entries_[i].Init(board_key_, hand_, depth_, pn, dn, amount);

    bool does_have_old_child{false};
    const auto result = query_.LookUp<false>(does_have_old_child, MateLen::Make(33, 4));

    EXPECT_EQ(result.Pn(), pn) << i;
    EXPECT_EQ(result.Dn(), dn) << i;
    EXPECT_EQ(result.Amount(), entries_[i].Amount()) << i;

    entries_[i].SetNull();
  }
}

TEST_F(V3QueryTest, LoopUp_UnknownExactRepetition) {
  rep_table_.Insert(path_key_);

  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_, hand_, depth_, pn, dn, amount);
  entries_[0].SetPossibleRepetition();
  entries_[0].UpdateUnknown(board_key_, pn, dn, MateLen16::Make(33, 4), 1);

  bool does_have_old_child{false};
  const auto result = query_.LookUp<false>(does_have_old_child, MateLen::Make(33, 4));

  EXPECT_EQ(result.Pn(), kInfinitePnDn);
  EXPECT_EQ(result.Dn(), 0);
  EXPECT_EQ(result.Amount(), entries_[0].Amount());
}

TEST_F(V3QueryTest, LoopUp_UnknownExactNoRepetition) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_, hand_, depth_, pn, dn, amount);
  entries_[0].SetPossibleRepetition();
  entries_[0].UpdateUnknown(board_key_, pn, dn, MateLen16::Make(33, 4), 1);

  bool does_have_old_child{false};
  const auto result = query_.LookUp<false>(does_have_old_child, MateLen::Make(33, 4));

  EXPECT_EQ(result.Pn(), pn);
  EXPECT_EQ(result.Dn(), dn);
  EXPECT_EQ(result.Amount(), entries_[0].Amount());
}

TEST_F(V3QueryTest, LoopUp_DifferentBoardKey) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_ ^ 0x01, hand_, depth_, pn, dn, amount);

  bool does_have_old_child{false};
  const auto result = query_.LookUp<false>(does_have_old_child, MateLen::Make(33, 4));

  EXPECT_EQ(result.Pn(), kPnDnUnit);
  EXPECT_EQ(result.Dn(), kPnDnUnit);
  EXPECT_EQ(result.Amount(), 1);
}

TEST_F(V3QueryTest, LoopUp_DifferentHand) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_, MakeHand<GOLD>(), depth_, pn, dn, amount);

  bool does_have_old_child{false};
  const auto result = query_.LookUp<false>(does_have_old_child, MateLen::Make(33, 4));

  EXPECT_EQ(result.Pn(), kPnDnUnit);
  EXPECT_EQ(result.Dn(), kPnDnUnit);
  EXPECT_EQ(result.Amount(), 1);
}

TEST_F(V3QueryTest, LoopUp_UnknownSuperior) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_, MakeHand<PAWN>(), depth_, pn, dn, amount);

  bool does_have_old_child{false};
  const auto result = query_.LookUp<false>(does_have_old_child, MateLen::Make(33, 4));

  EXPECT_EQ(result.Pn(), kPnDnUnit);
  EXPECT_EQ(result.Dn(), dn);
  EXPECT_EQ(result.Amount(), amount);
}

TEST_F(V3QueryTest, LoopUp_UnknownInferior) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_, MakeHand<PAWN, LANCE, LANCE, GOLD>(), depth_, pn, dn, amount);

  bool does_have_old_child{false};
  const auto result = query_.LookUp<false>(does_have_old_child, MateLen::Make(33, 4));

  EXPECT_EQ(result.Pn(), pn);
  EXPECT_EQ(result.Dn(), kPnDnUnit);
  EXPECT_EQ(result.Amount(), amount);

  entries_[0].SetNull();
}

TEST_F(V3QueryTest, LoopUp_Proven) {
  const auto hand = MakeHand<PAWN>();
  entries_[0].Init(board_key_, hand, depth_, 1, 1, 1);
  entries_[0].UpdateProven(MateLen16::Make(26, 4), MOVE_NONE, 1);

  bool does_have_old_child{false};
  const auto result = query_.LookUp<false>(does_have_old_child, MateLen::Make(33, 4));

  EXPECT_EQ(result.Pn(), 0);
  EXPECT_EQ(result.Dn(), kInfinitePnDn);
  EXPECT_EQ(result.Len(), MateLen::Make(26, 4));
  EXPECT_EQ(result.GetHand(), hand);
  EXPECT_EQ(result.Amount(), entries_[0].Amount());
}

TEST_F(V3QueryTest, LoopUp_Disproven) {
  const auto hand = MakeHand<PAWN, LANCE, LANCE, LANCE>();
  entries_[0].Init(board_key_, hand, depth_, 1, 1, 1);
  entries_[0].UpdateDisproven(MateLen16::Make(330, 4), MOVE_NONE, 1);

  bool does_have_old_child{false};
  const auto result = query_.LookUp<false>(does_have_old_child, MateLen::Make(33, 4));

  EXPECT_EQ(result.Pn(), kInfinitePnDn);
  EXPECT_EQ(result.Dn(), 0);
  EXPECT_EQ(result.Len(), MateLen::Make(330, 4));
  EXPECT_EQ(result.GetHand(), hand);
  EXPECT_EQ(result.Amount(), entries_[0].Amount());
}

TEST_F(V3QueryTest, LoopUp_CreationEmpty) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_, MakeHand<PAWN>(), depth_, pn, dn, amount);

  bool does_have_old_child{false};
  query_.LookUp<true>(does_have_old_child, MateLen::Make(33, 4));

  EXPECT_EQ(entries_[1].Pn(), kPnDnUnit);
  EXPECT_EQ(entries_[1].Dn(), dn);
  EXPECT_EQ(entries_[1].Amount(), 1);
}

TEST_F(V3QueryTest, LoopUp_CreationFull) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};

  entries_[0].Init(board_key_, MakeHand<PAWN>(), depth_, pn, dn, amount);
  for (std::size_t i = 1; i < Cluster::kSize; ++i) {
    // entries_[8] の探索量が最小になるように初期化
    entries_[i].Init(0x264, HAND_ZERO, 1, 1, 1, 1 + (8 - i) * (8 - i));
  }

  bool does_have_old_child{false};
  query_.LookUp<true>(does_have_old_child, MateLen::Make(33, 4));

  EXPECT_EQ(entries_[8].Pn(), kPnDnUnit);
  EXPECT_EQ(entries_[8].Dn(), dn);
  EXPECT_EQ(entries_[8].Amount(), 1);
}

TEST_F(V3QueryTest, SetResult_UnknownNew) {
  const PnDn pn{33};
  const PnDn dn{4};
  const SearchAmount amount{334};
  const UnknownData unknown_data{};
  const SearchResult result = SearchResult::MakeUnknown(pn, dn, hand_, MateLen::Make(33, 4), amount, unknown_data);

  query_.SetResult(result);
  EXPECT_EQ(entries_[0].Pn(), pn);
  EXPECT_EQ(entries_[0].Dn(), dn);
  EXPECT_EQ(entries_[0].Amount(), amount);
}

TEST_F(V3QueryTest, SetResult_UnknownUpdate) {
  for (std::size_t i = 0; i < Cluster::kSize; ++i) {
    const PnDn pn{33 * (i + 1)};
    const PnDn dn{4 * (i + 1)};
    const SearchAmount amount{static_cast<SearchAmount>(334 * (i + 1))};
    entries_[i].Init(board_key_, hand_, 334, 1, 1, 1);

    const UnknownData unknown_data{};
    const SearchResult result = SearchResult::MakeUnknown(pn, dn, hand_, MateLen::Make(33, 4), amount, unknown_data);

    query_.SetResult(result);
    EXPECT_EQ(entries_[i].Pn(), pn) << i;
    EXPECT_EQ(entries_[i].Dn(), dn) << i;
    EXPECT_EQ(entries_[i].Amount(), 1 + amount) << i;

    entries_[i].SetNull();
  }
}

TEST_F(V3QueryTest, SetResult_ProvenNew) {
  const auto hand = MakeHand<PAWN>();
  const MateLen len = MateLen::Make(33, 4);
  const SearchResult result = SearchResult::MakeFinal<true>(hand, len, 1);

  query_.SetResult(result);
  EXPECT_EQ(entries_[0].ProvenLen(), len.To16());
}

TEST_F(V3QueryTest, SetResult_ProvenUpdate) {
  const auto hand = MakeHand<PAWN>();
  const MateLen len = MateLen::Make(33, 4);
  const SearchResult result = SearchResult::MakeFinal<true>(hand, len, 1);

  entries_[2].Init(board_key_, hand, 334, 1, 1, 1);
  query_.SetResult(result);
  EXPECT_EQ(entries_[2].ProvenLen(), len.To16());
}

TEST_F(V3QueryTest, SetResult_DisprovenNew) {
  const auto hand = MakeHand<PAWN, LANCE, LANCE, GOLD>();
  const MateLen len = MateLen::Make(33, 4);
  const SearchResult result = SearchResult::MakeFinal<false>(hand, len, 1);

  query_.SetResult(result);
  EXPECT_EQ(entries_[0].DisprovenLen(), len.To16());
}

TEST_F(V3QueryTest, SetResult_DisprovenUpdate) {
  const auto hand = MakeHand<PAWN, LANCE, LANCE, GOLD>();
  const MateLen len = MateLen::Make(33, 4);
  const SearchResult result = SearchResult::MakeFinal<false>(hand, len, 1);

  entries_[2].Init(board_key_, hand, 334, 1, 1, 1);
  query_.SetResult(result);
  EXPECT_EQ(entries_[2].DisprovenLen(), len.To16());
}

TEST_F(V3QueryTest, SetResult_RepetitionNew) {
  const SearchAmount amount{334};
  const SearchResult result = SearchResult::MakeFinal<false, true>(hand_, MateLen::Make(33, 4), amount);

  query_.SetResult(result);
  EXPECT_EQ(entries_[0].Pn(), 1);
  EXPECT_EQ(entries_[0].Dn(), 1);
  EXPECT_EQ(entries_[0].Amount(), 1);
  EXPECT_TRUE(rep_table_.Contains(path_key_));
}

TEST_F(V3QueryTest, SetResult_RepetitionUpdate) {
  const SearchAmount amount{334};
  const SearchResult result = SearchResult::MakeFinal<false, true>(hand_, MateLen::Make(33, 4), amount);

  entries_[2].Init(board_key_, hand_, 334, 1, 1, 1);
  query_.SetResult(result);
  EXPECT_EQ(entries_[2].Pn(), 1);
  EXPECT_EQ(entries_[2].Dn(), 1);
  EXPECT_EQ(entries_[2].Amount(), 1);
  EXPECT_TRUE(rep_table_.Contains(path_key_));
}
