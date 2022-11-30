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
  auto& front = *tt_.begin();
  front.Init(0x334, HAND_ZERO);

  EXPECT_FALSE(front.IsNull());
  tt_.Resize(334);
  auto& front2 = *tt_.begin();
  EXPECT_TRUE(front2.IsNull());
}

TEST_F(RegularTableTest, Clear) {
  auto& front = *tt_.begin();
  front.Init(0x334, HAND_ZERO);

  EXPECT_FALSE(front.IsNull());
  tt_.Clear();
  EXPECT_TRUE(front.IsNull());
}

TEST_F(RegularTableTest, PointerOf) {
  auto p1 = tt_.PointerOf(0);
  auto p2 = tt_.PointerOf(std::numeric_limits<Key>::max() / 2);

  EXPECT_EQ(tt_.begin(), p1.data());
  EXPECT_LT(p1.data(), p2.data());
  EXPECT_LT(p2.data(), tt_.end());
}

TEST_F(RegularTableTest, CalculateHashRate_EmptyAfterClear) {
  for (auto&& entry : tt_) {
    entry.Init(0x334, HAND_ZERO);
  }

  EXPECT_GT(tt_.CalculateHashRate(), 0);
  tt_.Clear();
  EXPECT_EQ(tt_.CalculateHashRate(), 0);
}

TEST_F(RegularTableTest, CalculateHashRate_Full) {
  for (auto&& entry : tt_) {
    entry.Init(0x334, HAND_ZERO);
  }

  EXPECT_EQ(tt_.CalculateHashRate(), 1.0);
  tt_.Clear();
  EXPECT_EQ(tt_.CalculateHashRate(), 0);
}

TEST_F(RegularTableTest, CollectGarbage) {
  // 直接テストするのは難しいので、コール後にちゃんとエントリが消えているかどうかを調べる

  komori::SearchAmount i = 1;
  for (auto&& entry : tt_) {
    entry.Init(0x334, HAND_ZERO);
    entry.UpdateUnknown(0, 3, 3, i++, komori::BitSet64::Full(), 334, HAND_ZERO);
  }

  EXPECT_EQ(tt_.CalculateHashRate(), 1.0);
  tt_.CollectGarbage();
  EXPECT_LT(tt_.CalculateHashRate(), 1.0 - komori::tt::detail::kGcRemovalRatio + 0.1);
  EXPECT_GT(tt_.CalculateHashRate(), 1.0 - komori::tt::detail::kGcRemovalRatio - 0.1);
}

TEST_F(RegularTableTest, CompactEntries) {
  tt_.begin()->Init(std::numeric_limits<Key>::max(), HAND_ZERO);
  EXPECT_FALSE(tt_.begin()->IsNull());
  tt_.CompactEntries();
  EXPECT_TRUE(tt_.begin()->IsNull());
  EXPECT_FALSE((tt_.end() - 1)->IsNull());
}

TEST_F(RegularTableTest, SaveLoad) {
  const auto board_key1{0x334334334334334ull};
  const auto hand1 = MakeHand<PAWN, LANCE, LANCE>();
  const auto board_key2{0x264264264264264ull};
  const auto hand2 = MakeHand<PAWN>();

  auto p1 = tt_.PointerOf(board_key1);
  p1->Init(board_key1, hand1);
  p1->UpdateUnknown(334, 1, 1, komori::tt::detail::kTTSaveAmountThreshold + 1, komori::BitSet64::Full(), 0x334,
                    HAND_ZERO);
  auto p2 = tt_.PointerOf(board_key2);
  p2->Init(board_key2, hand2);
  p2->UpdateUnknown(334, 1, 1, komori::tt::detail::kTTSaveAmountThreshold - 1, komori::BitSet64::Full(), 0x334,
                    HAND_ZERO);
  ASSERT_NE(&*p1, &*p2);

  std::stringstream ss;
  tt_.Save(ss);
  tt_.Clear();
  EXPECT_FALSE(p1->IsFor(board_key1, hand1));
  EXPECT_FALSE(p2->IsFor(board_key2, hand2));

  // p1 の位置に entry2 を書き込む。次の load 後には entry2, entry1 の順に並ぶはず
  p1->Init(board_key2, hand2);

  tt_.Load(ss);
  EXPECT_TRUE(p1->IsFor(board_key2, hand2));
  EXPECT_TRUE((&*p1 + 1)->IsFor(board_key1, hand1));
  EXPECT_FALSE(p2->IsFor(board_key2, hand2));
}
