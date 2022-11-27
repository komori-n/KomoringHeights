#include <gtest/gtest.h>

#include "../regular_table.hpp"
#include "test_lib.hpp"

namespace {
class RegularTableTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tt_.Resize(2604);
    tt_.Clear();
  }

  komori::tt::RegularTable tt_;
};
}  // namespace

TEST_F(RegularTableTest, Resize_ChangeSize) {
  const std::size_t expected_size = 334;
  tt_.Resize(expected_size);

  EXPECT_EQ(tt_.end() - tt_.begin(), expected_size);
}

TEST_F(RegularTableTest, Resize_ClearEntries) {
  auto& head_entry = *tt_.ClusterOf(0).head_entry;
  head_entry.Init(0x334, HAND_ZERO);

  EXPECT_FALSE(head_entry.IsNull());
  tt_.Resize(334);
  auto& head_entry2 = *tt_.ClusterOf(0).head_entry;
  EXPECT_TRUE(head_entry2.IsNull());
}

TEST_F(RegularTableTest, Clear) {
  auto& head_entry = *tt_.ClusterOf(0).head_entry;
  head_entry.Init(0x334, HAND_ZERO);

  EXPECT_FALSE(head_entry.IsNull());
  tt_.Clear();
  EXPECT_TRUE(head_entry.IsNull());
}

TEST_F(RegularTableTest, ClusterOf) {
  auto e1 = tt_.ClusterOf(0).head_entry;
  auto e2 = tt_.ClusterOf(std::numeric_limits<Key>::max() / 2).head_entry;

  EXPECT_EQ(&*tt_.begin(), e1);
  EXPECT_LT(e1, e2);
  EXPECT_LT(e2, &*tt_.end());
}

TEST_F(RegularTableTest, CalculateHashRate_EmptyAfterClear) {
  for (auto itr = tt_.ClusterOf(0).head_entry; itr != &*tt_.begin() + (tt_.end() - tt_.begin()); ++itr) {
    itr->Init(0x334, HAND_ZERO);
  }

  EXPECT_GT(tt_.CalculateHashRate(), 0);
  tt_.Clear();
  EXPECT_EQ(tt_.CalculateHashRate(), 0);
}

TEST_F(RegularTableTest, CalculateHashRate_Full) {
  for (auto itr = tt_.ClusterOf(0).head_entry; itr != &*tt_.end(); ++itr) {
    itr->Init(0x334, HAND_ZERO);
  }

  EXPECT_EQ(tt_.CalculateHashRate(), 1.0);
  tt_.Clear();
  EXPECT_EQ(tt_.CalculateHashRate(), 0);
}

TEST_F(RegularTableTest, CollectGarbage) {
  // 近日変更予定なのでテストはしない
}

TEST_F(RegularTableTest, CompactEntries) {
  // 近日変更予定なのでテストはしない
}

TEST_F(RegularTableTest, SaveLoad) {
  const auto board_key1{0x334334334334334ull};
  const auto hand1 = MakeHand<PAWN, LANCE, LANCE>();
  const auto board_key2{0x264264264264264ull};
  const auto hand2 = MakeHand<PAWN>();

  const auto e1 = tt_.ClusterOf(board_key1).head_entry;
  e1->Init(board_key1, hand1);
  e1->UpdateUnknown(334, 1, 1, komori::tt::detail::kTTSaveAmountThreshold + 1, komori::BitSet64::Full(), 0x334,
                    HAND_ZERO);
  const auto e2 = tt_.ClusterOf(board_key2).head_entry;
  e2->Init(board_key2, hand2);
  e2->UpdateUnknown(334, 1, 1, komori::tt::detail::kTTSaveAmountThreshold - 1, komori::BitSet64::Full(), 0x334,
                    HAND_ZERO);
  ASSERT_NE(e1, e2);

  std::stringstream ss;
  tt_.Save(ss);
  tt_.Clear();
  EXPECT_FALSE(e1->IsFor(board_key1, hand1));
  EXPECT_FALSE(e2->IsFor(board_key2, hand2));

  // e1 の位置に entry2 を書き込む。次の load 後には entry2, entry1 の順に並ぶはず
  e1->Init(board_key2, hand2);

  tt_.Load(ss);
  EXPECT_TRUE(e1->IsFor(board_key2, hand2));
  EXPECT_TRUE((e1 + 1)->IsFor(board_key1, hand1));
  EXPECT_FALSE(e2->IsFor(board_key2, hand2));
}
