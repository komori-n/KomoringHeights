#include <gtest/gtest.h>

#include "../ttentry.hpp"
#include "test_lib.hpp"

using komori::BitSet64;
using komori::kDepthMaxPlus1MateLen16;
using komori::kMinus1MateLen16;
using komori::MateLen16;
using komori::PnDn;
using komori::SearchAmount;
using komori::tt::Entry;
using komori::tt::detail::kFinalAmountBonus;

TEST(EntryTest, DefaultConstructedInstanceIsNull) {
  Entry entry;
  EXPECT_TRUE(entry.IsNull());
}

TEST(EntryTest, Init_PossibleRepetition) {
  Entry entry;
  entry.Init(0x334334, HAND_ZERO);

  EXPECT_FALSE(entry.IsPossibleRepetition());
}

TEST(EntryTest, SetPossibleRepetition_PossibleRepetition) {
  Entry entry;
  entry.Init(0x334334, HAND_ZERO);
  entry.SetPossibleRepetition();

  EXPECT_TRUE(entry.IsPossibleRepetition());
}

TEST(EntryTest, IsFor) {
  Entry entry;
  const Key key{0x334334};
  const Hand hand{MakeHand<PAWN, LANCE>()};
  entry.Init(key, hand);

  EXPECT_TRUE(entry.IsFor(key));
  EXPECT_FALSE(entry.IsFor(0x264264));
  EXPECT_TRUE(entry.IsFor(key, hand));
  EXPECT_FALSE(entry.IsFor(0x264264, hand));
  EXPECT_FALSE(entry.IsFor(key, MakeHand<PAWN, LANCE, LANCE>()));
}

TEST(EntryTest, GetHand) {
  Entry entry;
  const Key key{0x334334};
  const Hand hand{MakeHand<PAWN, LANCE>()};
  entry.Init(key, hand);
  EXPECT_EQ(entry.GetHand(), hand);
}

TEST(EntryTest, Init_SumMask) {
  Entry entry;
  entry.Init(0x334, HAND_ZERO);
  EXPECT_EQ(entry.SumMask(), BitSet64::Full());
}

TEST(EntryTest, Init_Parent) {
  Entry entry;
  entry.Init(0x334, HAND_ZERO);
  EXPECT_EQ(entry.GetParentBoardKey(), komori::kNullKey);
  EXPECT_EQ(entry.GetParentHand(), komori::kNullHand);
}

TEST(EntryTest, UpdateUnknown_SumMask) {
  Entry entry;
  const BitSet64 bs{334};
  entry.Init(0x334, HAND_ZERO);
  entry.UpdateUnknown(0, 1, 1, 1, bs, 0, HAND_ZERO);
  EXPECT_EQ(entry.SumMask(), bs);
}

TEST(EntryTest, UpdateUnknown_MinDepth) {
  Entry entry;
  // depth1 と depth2 を両方せっとしたら小さい方が MinDepth() になる
  const Depth depth1{334};
  const Depth depth2{264};

  entry.Init(0x264, HAND_ZERO);
  entry.UpdateUnknown(depth1, 1, 1, 1, BitSet64::Full(), 0, HAND_ZERO);
  entry.UpdateUnknown(depth2, 1, 1, 1, BitSet64::Full(), 0, HAND_ZERO);
  EXPECT_EQ(entry.MinDepth(), depth2);

  entry.Init(0x264, HAND_ZERO);
  entry.UpdateUnknown(depth2, 1, 1, 1, BitSet64::Full(), 0, HAND_ZERO);
  entry.UpdateUnknown(depth1, 1, 1, 1, BitSet64::Full(), 0, HAND_ZERO);
  EXPECT_EQ(entry.MinDepth(), depth2);
}

TEST(EntryTest, UpdateUnknown_Parent) {
  Entry entry;
  const Key board_key{0x3304};
  const Hand hand{MakeHand<PAWN, LANCE, LANCE>()};
  entry.Init(0x264, HAND_ZERO);
  entry.UpdateUnknown(334, 1, 1, 1, BitSet64::Full(), board_key, hand);

  EXPECT_EQ(entry.GetParentBoardKey(), board_key);
  EXPECT_EQ(entry.GetParentHand(), hand);
}

TEST(EntryTest, LookUp_MinDepth) {
  Entry entry;
  const Hand hand{MakeHand<PAWN, LANCE, LANCE>()};
  const Depth depth1{334};
  const Depth depth2{264};
  const Depth depth3{2640};
  PnDn pn{1}, dn{1};
  MateLen16 len{334};
  bool use_old_child{false};

  entry.Init(0x264, hand);
  entry.UpdateUnknown(depth1, 1, 1, 1, BitSet64::Full(), 0, HAND_ZERO);
  entry.LookUp(MakeHand<PAWN, LANCE>(), depth2, len, pn, dn, use_old_child);
  EXPECT_EQ(entry.MinDepth(), depth1);  // 劣等局面では depth を更新しない

  entry.LookUp(hand, depth3, len, pn, dn, use_old_child);
  EXPECT_EQ(entry.MinDepth(), depth1);  // depth は最小値

  entry.LookUp(hand, depth2, len, pn, dn, use_old_child);
  EXPECT_EQ(entry.MinDepth(), depth2);  // depth は最小値
}

TEST(EntryTest, LookUp_PnDn_Exact) {
  Entry entry;
  const Hand hand{MakeHand<PAWN, LANCE, LANCE>()};
  const Depth depth1{334};
  const Depth depth2{2604};
  PnDn pn{1}, dn{1};
  MateLen16 len{334};
  bool use_old_child{false};

  entry.Init(0x264, hand);
  entry.UpdateUnknown(depth1, 33, 4, 1, BitSet64::Full(), 0, HAND_ZERO);
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

TEST(EntryTest, LookUp_PnDn_Superior) {
  Entry entry;
  const Hand hand1{MakeHand<PAWN, LANCE, LANCE>()};
  const Hand hand2{MakeHand<PAWN, LANCE, LANCE, LANCE, GOLD>()};
  const Depth depth1{334};
  const Depth depth2{3304};
  const Depth depth3{264};
  PnDn pn{1}, dn{1};
  MateLen16 len{334};
  bool use_old_child{false};

  entry.Init(0x264, hand1);
  entry.UpdateUnknown(depth1, 33, 4, 1, BitSet64::Full(), 0, HAND_ZERO);
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

TEST(EntryTest, LookUp_PnDn_Inferior) {
  Entry entry;
  const Hand hand1{MakeHand<PAWN, LANCE, LANCE>()};
  const Hand hand2{MakeHand<PAWN>()};
  const Depth depth1{334};
  const Depth depth2{3304};
  const Depth depth3{264};
  PnDn pn{1}, dn{1};
  MateLen16 len{334};
  bool use_old_child{false};

  entry.Init(0x264, hand1);
  entry.UpdateUnknown(depth1, 33, 4, 1, BitSet64::Full(), 0, HAND_ZERO);
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

TEST(EntryTest, LookUp_PnDn_Proven) {
  Entry entry;
  const Hand hand1{MakeHand<PAWN, LANCE, LANCE>()};
  const Hand hand2{MakeHand<PAWN, LANCE, LANCE, LANCE, GOLD>()};
  const MateLen16 len1{264};
  const MateLen16 len2{334};
  const Depth depth{334};
  PnDn pn{1}, dn{1};
  MateLen16 len{len2};
  bool use_old_child{false};

  entry.Init(0x264, hand1);
  entry.UpdateProven(len1, 1);
  // 現局面と一致
  const auto ret = entry.LookUp(hand1, depth, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret);
  EXPECT_EQ(pn, 0);
  EXPECT_EQ(dn, komori::kInfinitePnDn);

  // 優等局面（内部的には上の処理と別関数なのでテストが2つほしい）
  pn = dn = 1;
  const auto ret2 = entry.LookUp(hand2, depth, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret2);
  EXPECT_EQ(pn, 0);
  EXPECT_EQ(dn, komori::kInfinitePnDn);
}

TEST(EntryTest, LookUp_PnDn_Disproven) {
  Entry entry;
  const Hand hand1{MakeHand<PAWN, LANCE, LANCE>()};
  const Hand hand2{MakeHand<LANCE>()};
  const MateLen16 len1{334};
  const MateLen16 len2{264};
  const Depth depth{334};
  PnDn pn{1}, dn{1};
  MateLen16 len{len2};
  bool use_old_child{false};

  entry.Init(0x264, hand1);
  entry.UpdateDisproven(len1, 1);
  const auto ret = entry.LookUp(hand1, depth, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret);
  EXPECT_EQ(pn, komori::kInfinitePnDn);
  EXPECT_EQ(dn, 0);

  pn = dn = 1;
  const auto ret2 = entry.LookUp(hand2, depth, len, pn, dn, use_old_child);
  EXPECT_TRUE(ret2);
  EXPECT_EQ(pn, komori::kInfinitePnDn);
  EXPECT_EQ(dn, 0);
}

TEST(EntryTest, UpdateParentCandidate_DoNothing) {
  Entry entry;
  entry.Init(0x264, MakeHand<PAWN, LANCE, LANCE>());
  entry.UpdateUnknown(334, 33, 4, 1, BitSet64::Full(), 33, HAND_ZERO);
  PnDn pn{1};
  PnDn dn{1};
  Key parent_key{komori::kNullKey};
  Hand parent_hand{komori::kNullHand};

  // 関係ない局面を渡す -> ローカル変数は書き換わらないはず
  entry.UpdateParentCandidate(MakeHand<GOLD>(), pn, dn, parent_key, parent_hand);

  EXPECT_EQ(pn, 1);
  EXPECT_EQ(dn, 1);
  EXPECT_EQ(parent_key, komori::kNullKey);
  EXPECT_EQ(parent_hand, komori::kNullHand);
}

TEST(EntryTest, UpdateParentCandidate_UseInferiorParent) {
  Entry entry;
  entry.Init(0x264, MakeHand<PAWN, LANCE, LANCE>());
  entry.UpdateUnknown(334, 33, 4, 1, BitSet64::Full(), 334, MakeHand<LANCE, LANCE>());
  PnDn pn{1};
  PnDn dn{1};
  Key parent_key{komori::kNullKey};
  Hand parent_hand{komori::kNullHand};

  // 関係ない局面を渡す -> ローカル変数は書き換わらないはず
  entry.UpdateParentCandidate(MakeHand<PAWN>(), pn, dn, parent_key, parent_hand);

  EXPECT_EQ(pn, 33);
  EXPECT_EQ(dn, 1);
  EXPECT_EQ(parent_key, 334);
  EXPECT_EQ(parent_hand, HAND_ZERO);
}

TEST(EntryTest, UpdateParentCandidate_UseSuperiorParent) {
  Entry entry;
  entry.Init(0x264, MakeHand<PAWN, LANCE, LANCE>());
  entry.UpdateUnknown(334, 33, 4, 1, BitSet64::Full(), 334, MakeHand<LANCE, LANCE>());
  PnDn pn{1};
  PnDn dn{1};
  Key parent_key{komori::kNullKey};
  Hand parent_hand{komori::kNullHand};

  // 関係ない局面を渡す -> ローカル変数は書き換わらないはず
  entry.UpdateParentCandidate(MakeHand<PAWN, LANCE, LANCE, LANCE, GOLD>(), pn, dn, parent_key, parent_hand);

  EXPECT_EQ(pn, 1);
  EXPECT_EQ(dn, 4);
  EXPECT_EQ(parent_key, 334);
  EXPECT_EQ(parent_hand, (MakeHand<LANCE, LANCE, LANCE, GOLD>()));
}

TEST(EntryTest, SetPossibleRepetition_PnDn) {
  Entry entry;
  entry.Init(0x264, HAND_ZERO);
  entry.UpdateUnknown(334, 33, 4, 1, BitSet64::Full(), 0, HAND_ZERO);
  entry.SetPossibleRepetition();
  EXPECT_EQ(entry.Pn(), 1);
  EXPECT_EQ(entry.Dn(), 1);
}

TEST(EntryTest, Init_ProvenLen) {
  Entry entry;
  entry.Init(0x264, HAND_ZERO);
  EXPECT_EQ(entry.ProvenLen(), kDepthMaxPlus1MateLen16);
}

TEST(EntryTest, UpdateProven_ProvenLen) {
  Entry entry;
  const MateLen16 len1{334};
  const MateLen16 len2{3340};
  const MateLen16 len3{264};
  entry.Init(0x264, HAND_ZERO);
  entry.UpdateProven(len1, 1);
  EXPECT_EQ(entry.ProvenLen(), len1);

  entry.UpdateProven(len2, 1);
  EXPECT_EQ(entry.ProvenLen(), len1);

  entry.UpdateProven(len3, 1);
  EXPECT_EQ(entry.ProvenLen(), len3);
}

TEST(EntryTest, Init_DisprovenLen) {
  Entry entry;
  entry.Init(0x264, HAND_ZERO);
  EXPECT_EQ(entry.DisprovenLen(), kMinus1MateLen16);
}

TEST(EntryTest, UpdateProven_DisprovenLen) {
  Entry entry;
  const MateLen16 len1{334};
  const MateLen16 len2{264};
  const MateLen16 len3{3340};
  entry.Init(0x264, HAND_ZERO);
  entry.UpdateDisproven(len1, 1);
  EXPECT_EQ(entry.DisprovenLen(), len1);

  entry.UpdateDisproven(len2, 1);
  EXPECT_EQ(entry.DisprovenLen(), len1);

  entry.UpdateDisproven(len3, 1);
  EXPECT_EQ(entry.DisprovenLen(), len3);
}

TEST(EntryTest, LookUp_UseOldChild_Superior) {
  Entry entry;
  const Hand hand1{MakeHand<PAWN, LANCE, LANCE>()};
  const Hand hand2{MakeHand<PAWN, LANCE, LANCE, LANCE, GOLD>()};
  const Depth depth1{334};
  const Depth depth2{2604};
  const Depth depth3{264};
  PnDn pn{1}, dn{1};
  MateLen16 len{334};
  bool use_old_child{false};

  entry.Init(0x264, hand1);
  entry.UpdateUnknown(depth1, 33, 4, 1, BitSet64::Full(), 0, HAND_ZERO);
  entry.LookUp(hand2, depth2, len, pn, dn, use_old_child);
  EXPECT_TRUE(use_old_child);

  use_old_child = false;
  entry.LookUp(hand2, depth3, len, pn, dn, use_old_child);
  EXPECT_FALSE(use_old_child);
}

TEST(EntryTest, LookUp_UseOldChild_Inferior) {
  Entry entry;
  const Hand hand1{MakeHand<PAWN, LANCE, LANCE>()};
  const Hand hand2{MakeHand<PAWN>()};
  const Depth depth1{334};
  const Depth depth2{2604};
  const Depth depth3{264};
  PnDn pn{1}, dn{1};
  MateLen16 len{334};
  bool use_old_child{false};

  entry.Init(0x264, hand1);
  entry.UpdateUnknown(depth1, 33, 4, 1, BitSet64::Full(), 0, HAND_ZERO);
  entry.LookUp(hand2, depth2, len, pn, dn, use_old_child);
  EXPECT_TRUE(use_old_child);

  use_old_child = false;
  entry.LookUp(hand2, depth3, len, pn, dn, use_old_child);
  EXPECT_FALSE(use_old_child);
}

TEST(EntryTest, UpdateUnknown_Amount) {
  Entry entry;
  const SearchAmount amount{334};
  entry.Init(0x264, HAND_ZERO);
  entry.UpdateUnknown(264, 26, 4, amount, BitSet64::Full(), 0, HAND_ZERO);
  EXPECT_EQ(entry.Amount(), 1 / 2 + amount);
}

TEST(EntryTest, UpdateUnknown_SaturatedAmount) {
  Entry entry;
  const SearchAmount amount{std::numeric_limits<SearchAmount>::max()};
  entry.Init(0x264, HAND_ZERO);
  entry.UpdateUnknown(264, 26, 4, amount, BitSet64::Full(), 0, HAND_ZERO);
  EXPECT_EQ(entry.Amount(), amount);
}

TEST(EntryTest, UpdateProven_Amount) {
  Entry entry;
  const SearchAmount amount1{334};
  const SearchAmount amount2{264};
  entry.Init(0x264, HAND_ZERO);
  entry.UpdateUnknown(264, 26, 4, amount1, BitSet64::Full(), 0, HAND_ZERO);
  entry.UpdateProven(MateLen16{334}, amount2);
  EXPECT_EQ(entry.Amount(), amount2 + kFinalAmountBonus);
}

TEST(EntryTest, UpdateDisproven_Amount) {
  Entry entry;
  const SearchAmount amount1{334};
  const SearchAmount amount2{264};
  entry.Init(0x264, HAND_ZERO);
  entry.UpdateUnknown(264, 26, 4, amount1, BitSet64::Full(), 0, HAND_ZERO);
  entry.UpdateDisproven(MateLen16{334}, amount2);
  EXPECT_EQ(entry.Amount(), amount2 + kFinalAmountBonus);
}

TEST(EntryTest, UpdateFinalRange_Superior) {
  Entry entry;
  const MateLen16 len{334};
  entry.Init(0x264, MakeHand<PAWN, LANCE, LANCE, GOLD>());
  entry.UpdateDisproven(len, 1);

  MateLen16 disproven_len = kMinus1MateLen16;
  MateLen16 proven_len = kDepthMaxPlus1MateLen16;
  entry.UpdateFinalRange(MakeHand<PAWN, LANCE>(), disproven_len, proven_len);
  EXPECT_EQ(disproven_len, len);
  EXPECT_EQ(proven_len, kDepthMaxPlus1MateLen16);

  disproven_len = len + 1;
  entry.UpdateFinalRange(MakeHand<PAWN, LANCE>(), disproven_len, proven_len);
  EXPECT_EQ(disproven_len, len + 1);
}

TEST(EntryTest, UpdateFinalRange_Inferior) {
  Entry entry;
  const MateLen16 len{334};
  entry.Init(0x264, HAND_ZERO);
  entry.UpdateProven(len, 1);

  MateLen16 disproven_len = kMinus1MateLen16;
  MateLen16 proven_len = kDepthMaxPlus1MateLen16;
  entry.UpdateFinalRange(MakeHand<PAWN, LANCE>(), disproven_len, proven_len);
  EXPECT_EQ(disproven_len, kMinus1MateLen16);
  EXPECT_EQ(proven_len, len);

  proven_len = len - 1;
  entry.UpdateFinalRange(MakeHand<PAWN, LANCE>(), disproven_len, proven_len);
  EXPECT_EQ(proven_len, len - 1);
}
