#include <gtest/gtest.h>

#include "../search_result.hpp"
#include "test_lib.hpp"

using komori::BitSet64;
using komori::FinalData;
using komori::MateLen;
using komori::NodeState;
using komori::SearchResult;
using komori::SearchResultComparer;
using komori::UnknownData;

TEST(SearchResultTest, ConstructUnknown) {
  const UnknownData unknown_data{true, BitSet64{445}};
  const auto result = SearchResult::MakeUnknown(33, 4, MateLen{264}, 10, unknown_data);

  EXPECT_EQ(result.Pn(), 33);
  EXPECT_EQ(result.Dn(), 4);
  EXPECT_EQ(result.Len(), MateLen{264});
  EXPECT_EQ(result.Amount(), 10);
  EXPECT_FALSE(result.IsFinal());
  EXPECT_TRUE(result.GetUnknownData().is_first_visit);
  EXPECT_EQ(result.GetUnknownData().sum_mask, BitSet64{445});
  EXPECT_EQ(result.GetNodeState(), NodeState::kUnknown);
}

TEST(SearchResultTest, MakeProven) {
  const auto result = SearchResult::MakeFinal<true>(MakeHand<PAWN, SILVER>(), MateLen{334}, 20);

  EXPECT_EQ(result.Pn(), 0);
  EXPECT_EQ(result.Dn(), komori::kInfinitePnDn);
  EXPECT_EQ(result.Len(), MateLen{334});
  EXPECT_EQ(result.Amount(), 20);
  EXPECT_TRUE(result.IsFinal());
  EXPECT_FALSE(result.GetFinalData().IsRepetition());
  EXPECT_EQ(result.GetFinalData().hand, (MakeHand<PAWN, SILVER>()));
  EXPECT_EQ(result.GetNodeState(), NodeState::kProven);
}

TEST(SearchResultTest, MakeDisproven) {
  const auto result = SearchResult::MakeFinal<false>(MakeHand<GOLD, GOLD>(), MateLen{334}, 30);

  EXPECT_EQ(result.Pn(), komori::kInfinitePnDn);
  EXPECT_EQ(result.Dn(), 0);
  EXPECT_EQ(result.Len(), MateLen{334});
  EXPECT_EQ(result.Amount(), 30);
  EXPECT_TRUE(result.IsFinal());
  EXPECT_FALSE(result.GetFinalData().IsRepetition());
  EXPECT_EQ(result.GetFinalData().hand, (MakeHand<GOLD, GOLD>()));
  EXPECT_EQ(result.GetNodeState(), NodeState::kDisproven);
}

TEST(SearchResultTest, MakeRepetition) {
  const auto result = SearchResult::MakeRepetition(MakeHand<ROOK, BISHOP>(), MateLen{334}, 40, 334);

  EXPECT_EQ(result.Pn(), komori::kInfinitePnDn);
  EXPECT_EQ(result.Dn(), 0);
  EXPECT_EQ(result.Len(), MateLen{334});
  EXPECT_EQ(result.Amount(), 40);
  EXPECT_TRUE(result.IsFinal());
  EXPECT_EQ(result.GetFinalData().repetition_start, 334);
  EXPECT_EQ(result.GetFinalData().hand, (MakeHand<ROOK, BISHOP>()));
  EXPECT_EQ(result.GetNodeState(), NodeState::kRepetition);
}

TEST(SearchResultTest, Phi) {
  const auto result = SearchResult::MakeFinal<true>(HAND_ZERO, MateLen{334}, 20);

  EXPECT_EQ(result.Phi(true), 0);
  EXPECT_EQ(result.Phi(false), komori::kInfinitePnDn);
}

TEST(SearchResultTest, Delta) {
  const auto result = SearchResult::MakeFinal<true>(HAND_ZERO, MateLen{334}, 20);

  EXPECT_EQ(result.Delta(true), komori::kInfinitePnDn);
  EXPECT_EQ(result.Delta(false), 0);
}

TEST(SearchResultTest, Normal) {
  const UnknownData unknown_data{true, BitSet64{445}};
  const auto result = SearchResult::MakeUnknown(33, 4, MateLen{264}, 10, unknown_data);

  komori::PnDn thpn = 1;
  komori::PnDn thdn = 1;
  ExtendSearchThreshold(result, thpn, thdn);
  EXPECT_EQ(thpn, 33 + 1);
  EXPECT_EQ(thdn, 4 + 1);
}

TEST(SearchResultTest, Final) {
  const auto result = SearchResult::MakeFinal<true>(MakeHand<PAWN, SILVER>(), MateLen{334}, 20);

  komori::PnDn thpn = 1;
  komori::PnDn thdn = 1;
  ExtendSearchThreshold(result, thpn, thdn);
  EXPECT_EQ(thpn, 1);
  EXPECT_EQ(thdn, 1);
}

TEST(SearchResultTest, Infinite) {
  using komori::kInfinitePnDn;
  const UnknownData unknown_data{true, BitSet64{445}};
  const auto result = SearchResult::MakeUnknown(kInfinitePnDn, kInfinitePnDn, MateLen{264}, 10, unknown_data);

  komori::PnDn thpn = 1;
  komori::PnDn thdn = 1;
  ExtendSearchThreshold(result, thpn, thdn);
  EXPECT_EQ(thpn, 1);
  EXPECT_EQ(thdn, 1);
}

TEST(SearchResultComparerTest, OrNode) {
  SearchResultComparer sr_comparer{true};

  const UnknownData unknown_data{true, BitSet64{445}};
  const auto u1 = SearchResult::MakeUnknown(33, 4, MateLen{264}, 10, unknown_data);
  const auto u2 = SearchResult::MakeUnknown(26, 4, MateLen{264}, 10, unknown_data);
  const auto u3 = SearchResult::MakeUnknown(33, 5, MateLen{264}, 10, unknown_data);
  const auto u4 = SearchResult::MakeUnknown(33, 4, MateLen{264}, 13, unknown_data);
  const auto f1 = SearchResult::MakeFinal<false>(MakeHand<PAWN, SILVER>(), MateLen{334}, 20);
  const auto f2 = SearchResult::MakeRepetition(MakeHand<PAWN, SILVER>(), MateLen{334}, 20, 0);
  const auto f3 = SearchResult::MakeFinal<false>(MakeHand<PAWN, SILVER>(), MateLen{334}, 24);

  EXPECT_EQ(sr_comparer(u2, u1), SearchResultComparer::Ordering::kLess);
  EXPECT_EQ(sr_comparer(u1, u2), SearchResultComparer::Ordering::kGreater);
  EXPECT_EQ(sr_comparer(u1, u3), SearchResultComparer::Ordering::kLess);
  EXPECT_EQ(sr_comparer(u3, u1), SearchResultComparer::Ordering::kGreater);
  EXPECT_EQ(sr_comparer(u1, u4), SearchResultComparer::Ordering::kLess);
  EXPECT_EQ(sr_comparer(u4, u1), SearchResultComparer::Ordering::kGreater);

  EXPECT_EQ(sr_comparer(f1, f2), SearchResultComparer::Ordering::kGreater);
  EXPECT_EQ(sr_comparer(f2, f1), SearchResultComparer::Ordering::kLess);
  EXPECT_EQ(sr_comparer(f1, f3), SearchResultComparer::Ordering::kLess);
  EXPECT_EQ(sr_comparer(f3, f1), SearchResultComparer::Ordering::kGreater);

  EXPECT_EQ(sr_comparer(u1, u1), SearchResultComparer::Ordering::kEquivalent);
  EXPECT_EQ(sr_comparer(f1, f1), SearchResultComparer::Ordering::kEquivalent);
}
