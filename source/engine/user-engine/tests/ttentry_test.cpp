#include <gtest/gtest.h>

#include "../ttentry.hpp"
#include "test_lib.hpp"

using komori::kInfiniteMateLen16;
using komori::kMinusZeroMateLen16;
using komori::MateLen16;
using komori::PnDn;
using komori::tt::Entry;
using komori::tt::SearchAmount;
using komori::tt::detail::kFinalAmountBonus;

TEST(V3EntryTest, DefaultConstructedInstanceIsNull) {
  Entry entry;
  EXPECT_TRUE(entry.IsNull());
}

TEST(V3EntryTest, Init_PossibleRepetition) {
  Entry entry;
  entry.Init(0x334334, HAND_ZERO, 334, 1, 1, 1);

  EXPECT_FALSE(entry.IsPossibleRepetition());
}

TEST(V3EntryTest, SetPossibleRepetition_PossibleRepetition) {
  Entry entry;
  entry.Init(0x334334, HAND_ZERO, 334, 1, 1, 1);
  entry.SetPossibleRepetition();

  EXPECT_TRUE(entry.IsPossibleRepetition());
}

TEST(V3EntryTest, IsFor) {
  Entry entry;
  const Key key{0x334334};
  const Hand hand{MakeHand<PAWN, LANCE>()};
  entry.Init(key, hand, 334, 1, 1, 1);

  EXPECT_TRUE(entry.IsFor(key));
  EXPECT_FALSE(entry.IsFor(0x264264));
  EXPECT_TRUE(entry.IsFor(key, hand));
  EXPECT_FALSE(entry.IsFor(0x264264, hand));
  EXPECT_FALSE(entry.IsFor(key, MakeHand<PAWN, LANCE, LANCE>()));
}

TEST(V3EntryTest, GetHand) {
  Entry entry;
  const Key key{0x334334};
  const Hand hand{MakeHand<PAWN, LANCE>()};
  entry.Init(key, hand, 334, 1, 1, 1);
  EXPECT_EQ(entry.GetHand(), hand);
}

TEST(V3EntryTest, InitMinDepth) {
  Entry entry;
  const Depth depth{334};
  entry.Init(0x264, HAND_ZERO, depth, 1, 1, 1);
  EXPECT_EQ(entry.MinDepth(), depth);
}

TEST(V3EntryTest, UpdateUnknown_MinDepth) {
  Entry entry;
  // depth1 と depth2 を両方せっとしたら小さい方が MinDepth() になる
  const Depth depth1{334};
  const Depth depth2{264};

  entry.Init(0x264, HAND_ZERO, depth1, 1, 1, 1);
  entry.UpdateUnknown(depth2, 1, 1, MateLen16::Make(33, 4), 1);
  EXPECT_EQ(entry.MinDepth(), depth2);

  entry.Init(0x264, HAND_ZERO, depth2, 1, 1, 1);
  entry.UpdateUnknown(depth1, 1, 1, MateLen16::Make(33, 4), 1);
  EXPECT_EQ(entry.MinDepth(), depth2);
}

TEST(V3EntryTest, LookUp_MinDepth) {
  Entry entry;
  const Hand hand{MakeHand<PAWN, LANCE, LANCE>()};
  const Depth depth1{334};
  const Depth depth2{264};
  const Depth depth3{2640};
  PnDn pn{1}, dn{1};
  MateLen16 len{MateLen16::Make(33, 4)};
  bool use_old_child{false};

  entry.Init(0x264, hand, depth1, 1, 1, 1);
  entry.LookUp(MakeHand<PAWN, LANCE>(), depth2, len, pn, dn, use_old_child);
  EXPECT_EQ(entry.MinDepth(), depth1);  // 劣等局面では depth を更新しない

  entry.LookUp(hand, depth3, len, pn, dn, use_old_child);
  EXPECT_EQ(entry.MinDepth(), depth1);  // depth は最小値

  entry.LookUp(hand, depth2, len, pn, dn, use_old_child);
  EXPECT_EQ(entry.MinDepth(), depth2);  // depth は最小値
}

TEST(V3EntryTest, LookUp_PnDn_Exact) {
  Entry entry;
  const Hand hand{MakeHand<PAWN, LANCE, LANCE>()};
  const Depth depth1{334};
  const Depth depth2{2604};
  PnDn pn{1}, dn{1};
  MateLen16 len{MateLen16::Make(33, 4)};
  bool use_old_child{false};

  entry.Init(0x264, hand, depth1, 33, 4, 1);
  const auto ret1 = entry.LookUp(hand, depth1, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret1);
  EXPECT_EQ(pn, 33);
  EXPECT_EQ(dn, 4);

  pn = dn = 1;
  const auto ret2 = entry.LookUp(hand, depth2, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret2);
  EXPECT_EQ(pn, 33);
  EXPECT_EQ(dn, 4);

  pn = dn = 100;
  const auto ret3 = entry.LookUp(hand, depth2, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret3);
  EXPECT_EQ(pn, 100);
  EXPECT_EQ(dn, 100);
}

TEST(V3EntryTest, LookUp_PnDn_Superior) {
  Entry entry;
  const Hand hand1{MakeHand<PAWN, LANCE, LANCE>()};
  const Hand hand2{MakeHand<PAWN, LANCE, LANCE, LANCE, GOLD>()};
  const Depth depth1{334};
  const Depth depth2{3304};
  const Depth depth3{264};
  PnDn pn{1}, dn{1};
  MateLen16 len{MateLen16::Make(33, 4)};
  bool use_old_child{false};

  entry.Init(0x264, hand1, depth1, 33, 4, 1);
  const auto ret1 = entry.LookUp(hand2, depth2, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret1);
  EXPECT_EQ(pn, 1);
  EXPECT_EQ(dn, 4);

  pn = dn = 100;
  const auto ret2 = entry.LookUp(hand2, depth2, len, pn, dn, use_old_child);
  EXPECT_FALSE(ret2);
  EXPECT_EQ(pn, 100);
  EXPECT_EQ(dn, 100);

  pn = dn = 1;
  const auto ret3 = entry.LookUp(hand2, depth3, len, pn, dn, use_old_child);
  EXPECT_FALSE(ret3);
  EXPECT_EQ(pn, 1);
  EXPECT_EQ(dn, 1);
}

TEST(V3EntryTest, LookUp_PnDn_Inferior) {
  Entry entry;
  const Hand hand1{MakeHand<PAWN, LANCE, LANCE>()};
  const Hand hand2{MakeHand<PAWN>()};
  const Depth depth1{334};
  const Depth depth2{3304};
  const Depth depth3{264};
  PnDn pn{1}, dn{1};
  MateLen16 len{MateLen16::Make(33, 4)};
  bool use_old_child{false};

  entry.Init(0x264, hand1, depth1, 33, 4, 1);
  const auto ret1 = entry.LookUp(hand2, depth2, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret1);
  EXPECT_EQ(pn, 33);
  EXPECT_EQ(dn, 1);

  pn = dn = 100;
  const auto ret2 = entry.LookUp(hand2, depth2, len, pn, dn, use_old_child);
  EXPECT_FALSE(ret2);
  EXPECT_EQ(pn, 100);
  EXPECT_EQ(dn, 100);

  pn = dn = 1;
  const auto ret3 = entry.LookUp(hand2, depth3, len, pn, dn, use_old_child);
  EXPECT_FALSE(ret3);
  EXPECT_EQ(pn, 1);
  EXPECT_EQ(dn, 1);
}

TEST(V3EntryTest, LookUp_PnDn_Proven) {
  Entry entry;
  const Hand hand1{MakeHand<PAWN, LANCE, LANCE>()};
  const Hand hand2{MakeHand<PAWN, LANCE, LANCE, LANCE, GOLD>()};
  const MateLen16 len1{MateLen16::Make(26, 4)};
  const MateLen16 len2{MateLen16::Make(33, 4)};
  const Depth depth1{334};
  const Depth depth2{2604};
  PnDn pn{1}, dn{1};
  MateLen16 len{len2};
  bool use_old_child{false};

  entry.Init(0x264, hand1, depth1, 33, 4, 1);
  entry.UpdateProven(len1, 1);
  // 現局面と一致
  const auto ret = entry.LookUp(hand1, depth2, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret);
  EXPECT_EQ(pn, 0);
  EXPECT_EQ(dn, komori::kInfinitePnDn);

  // 優等局面（内部的には上の処理と別関数なのでテストが2つほしい）
  pn = dn = 1;
  const auto ret2 = entry.LookUp(hand2, depth2, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret2);
  EXPECT_EQ(pn, 0);
  EXPECT_EQ(dn, komori::kInfinitePnDn);
}

TEST(V3EntryTest, LookUp_PnDn_Disproven) {
  Entry entry;
  const Hand hand1{MakeHand<PAWN, LANCE, LANCE>()};
  const Hand hand2{MakeHand<LANCE>()};
  const MateLen16 len1{MateLen16::Make(33, 4)};
  const MateLen16 len2{MateLen16::Make(26, 4)};
  const Depth depth1{2604};
  const Depth depth2{334};
  PnDn pn{1}, dn{1};
  MateLen16 len{len2};
  bool use_old_child{false};

  entry.Init(0x264, hand1, depth1, 33, 4, 1);
  entry.UpdateDisproven(len1, 1);
  const auto ret = entry.LookUp(hand1, depth2, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret);
  EXPECT_EQ(pn, komori::kInfinitePnDn);
  EXPECT_EQ(dn, 0);

  pn = dn = 1;
  const auto ret2 = entry.LookUp(hand2, depth2, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret2);
  EXPECT_EQ(pn, komori::kInfinitePnDn);
  EXPECT_EQ(dn, 0);
}

TEST(V3EntryTest, Update_PnDn_Proven) {
  Entry entry;
  const MateLen16 len1{MateLen16::Make(33, 4)};
  const MateLen16 len2{MateLen16::Make(334, 0)};
  entry.Init(0x264, HAND_ZERO, 334, 1, 1, 1);

  entry.UpdateProven(len1, 1);
  entry.UpdateUnknown(334, 33, 4, len2, 1);
  EXPECT_EQ(entry.Pn(), 1);
  EXPECT_EQ(entry.Dn(), 1);
}

TEST(V3EntryTest, Update_PnDn_Disproven) {
  Entry entry;
  const MateLen16 len1{MateLen16::Make(33, 4)};
  const MateLen16 len2{MateLen16::Make(26, 4)};
  entry.Init(0x264, HAND_ZERO, 334, 1, 1, 1);

  entry.UpdateDisproven(len1, 1);
  entry.UpdateUnknown(334, 33, 4, len2, 1);
  EXPECT_EQ(entry.Pn(), 1);
  EXPECT_EQ(entry.Dn(), 1);
}

TEST(V3EntryTest, SetPossibleRepetition_PnDn) {
  Entry entry;
  entry.Init(0x264, HAND_ZERO, 334, 33, 4, 1);
  entry.SetPossibleRepetition();
  EXPECT_EQ(entry.Pn(), 1);
  EXPECT_EQ(entry.Dn(), 1);
}

TEST(V3EntryTest, Init_ProvenLen) {
  Entry entry;
  entry.Init(0x264, HAND_ZERO, 334, 1, 1, 1);
  EXPECT_EQ(entry.ProvenLen(), kInfiniteMateLen16);
}

TEST(V3EntryTest, UpdateProven_ProvenLen) {
  Entry entry;
  const MateLen16 len1{MateLen16::Make(33, 4)};
  const MateLen16 len2{MateLen16::Make(334, 0)};
  const MateLen16 len3{MateLen16::Make(26, 4)};
  entry.Init(0x264, HAND_ZERO, 334, 1, 1, 1);
  entry.UpdateProven(len1, 1);
  EXPECT_EQ(entry.ProvenLen(), len1);

  entry.UpdateProven(len2, 1);
  EXPECT_EQ(entry.ProvenLen(), len1);

  entry.UpdateProven(len3, 1);
  EXPECT_EQ(entry.ProvenLen(), len3);
}

TEST(V3EntryTest, Init_DisprovenLen) {
  Entry entry;
  entry.Init(0x264, HAND_ZERO, 334, 1, 1, 1);
  EXPECT_EQ(entry.DisprovenLen(), kMinusZeroMateLen16);
}

TEST(V3EntryTest, UpdateProven_DisprovenLen) {
  Entry entry;
  const MateLen16 len1{MateLen16::Make(33, 4)};
  const MateLen16 len2{MateLen16::Make(26, 4)};
  const MateLen16 len3{MateLen16::Make(334, 0)};
  entry.Init(0x264, HAND_ZERO, 334, 1, 1, 1);
  entry.UpdateDisproven(len1, 1);
  EXPECT_EQ(entry.DisprovenLen(), len1);

  entry.UpdateDisproven(len2, 1);
  EXPECT_EQ(entry.DisprovenLen(), len1);

  entry.UpdateDisproven(len3, 1);
  EXPECT_EQ(entry.DisprovenLen(), len3);
}

TEST(V3EntryTest, LookUp_UseOldChild_Superior) {
  Entry entry;
  const Hand hand1{MakeHand<PAWN, LANCE, LANCE>()};
  const Hand hand2{MakeHand<PAWN, LANCE, LANCE, LANCE, GOLD>()};
  const Depth depth1{334};
  const Depth depth2{2604};
  const Depth depth3{264};
  PnDn pn{1}, dn{1};
  MateLen16 len{MateLen16::Make(33, 4)};
  bool use_old_child{false};

  entry.Init(0x264, hand1, depth1, 33, 4, 1);
  entry.LookUp(hand2, depth2, len, pn, dn, use_old_child);
  EXPECT_TRUE(use_old_child);

  use_old_child = false;
  entry.LookUp(hand2, depth3, len, pn, dn, use_old_child);
  EXPECT_FALSE(use_old_child);
}

TEST(V3EntryTest, LookUp_UseOldChild_Inferior) {
  Entry entry;
  const Hand hand1{MakeHand<PAWN, LANCE, LANCE>()};
  const Hand hand2{MakeHand<PAWN>()};
  const Depth depth1{334};
  const Depth depth2{2604};
  const Depth depth3{264};
  PnDn pn{1}, dn{1};
  MateLen16 len{MateLen16::Make(33, 4)};
  bool use_old_child{false};

  entry.Init(0x264, hand1, depth1, 33, 4, 1);
  entry.LookUp(hand2, depth2, len, pn, dn, use_old_child);
  EXPECT_TRUE(use_old_child);

  use_old_child = false;
  entry.LookUp(hand2, depth3, len, pn, dn, use_old_child);
  EXPECT_FALSE(use_old_child);
}

TEST(V3EntryTest, Init_Amount) {
  Entry entry;
  const SearchAmount amount{334};
  entry.Init(0x264, HAND_ZERO, 264, 26, 4, amount);
  EXPECT_EQ(entry.Amount(), amount);
}

TEST(V3EntryTest, UpdateUnknown_Amount) {
  Entry entry;
  const SearchAmount amount1{334};
  const SearchAmount amount2{264};
  entry.Init(0x264, HAND_ZERO, 264, 26, 4, amount1);
  entry.UpdateUnknown(264, 26, 4, MateLen16::Make(33, 4), amount2);
  EXPECT_EQ(entry.Amount(), amount1 / 2 + amount2);
}

TEST(V3EntryTest, UpdateUnknown_SaturatedAmount) {
  Entry entry;
  const SearchAmount amount1{334};
  const SearchAmount amount2{std::numeric_limits<SearchAmount>::max()};
  entry.Init(0x264, HAND_ZERO, 264, 26, 4, amount1);
  entry.UpdateUnknown(264, 26, 4, MateLen16::Make(33, 4), amount2);
  EXPECT_EQ(entry.Amount(), amount2);
}

TEST(V3EntryTest, UpdateProven_Amount) {
  Entry entry;
  const SearchAmount amount1{334};
  const SearchAmount amount2{264};
  entry.Init(0x264, HAND_ZERO, 264, 26, 4, amount1);
  entry.UpdateProven(MateLen16::Make(33, 4), amount2);
  EXPECT_EQ(entry.Amount(), amount1 / 2 + amount2 + kFinalAmountBonus);
}

TEST(V3EntryTest, UpdateDisproven_Amount) {
  Entry entry;
  const SearchAmount amount1{334};
  const SearchAmount amount2{264};
  entry.Init(0x264, HAND_ZERO, 264, 26, 4, amount1);
  entry.UpdateDisproven(MateLen16::Make(33, 4), amount2);
  EXPECT_EQ(entry.Amount(), amount1 / 2 + amount2 + kFinalAmountBonus);
}

TEST(V3EntryTest, UpdateFinalRange_Superior) {
  Entry entry;
  const MateLen16 len = MateLen16::Make(33, 4);
  entry.Init(0x264, MakeHand<PAWN, LANCE, LANCE, GOLD>(), 264, 1, 1, 1);
  entry.UpdateDisproven(len, 1);

  MateLen16 disproven_len = kMinusZeroMateLen16;
  MateLen16 proven_len = kInfiniteMateLen16;
  entry.UpdateFinalRange(MakeHand<PAWN, LANCE>(), disproven_len, proven_len);
  EXPECT_EQ(disproven_len, len);
  EXPECT_EQ(proven_len, kInfiniteMateLen16);

  disproven_len = len + 1;
  entry.UpdateFinalRange(MakeHand<PAWN, LANCE>(), disproven_len, proven_len);
  EXPECT_EQ(disproven_len, len + 1);
}

TEST(V3EntryTest, UpdateFinalRange_Inferior) {
  Entry entry;
  const MateLen16 len = MateLen16::Make(33, 4);
  entry.Init(0x264, HAND_ZERO, 264, 1, 1, 1);
  entry.UpdateProven(len, 1);

  MateLen16 disproven_len = kMinusZeroMateLen16;
  MateLen16 proven_len = kInfiniteMateLen16;
  entry.UpdateFinalRange(MakeHand<PAWN, LANCE>(), disproven_len, proven_len);
  EXPECT_EQ(disproven_len, kMinusZeroMateLen16);
  EXPECT_EQ(proven_len, len);

  proven_len = len - 1;
  entry.UpdateFinalRange(MakeHand<PAWN, LANCE>(), disproven_len, proven_len);
  EXPECT_EQ(proven_len, len - 1);
}
