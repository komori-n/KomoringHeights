#include <gtest/gtest.h>

#include "../regular_table.hpp"
#include "test_lib.hpp"

namespace {
class CircularEntryPointerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto begin = vals_.data();
    auto back = vals_.data() + (vals_.size() - 1);
    auto end = vals_.data() + vals_.size();
    p_begin_ = komori::tt::CircularEntryPointer{begin, begin, end};
    p_begin_plus_1_ = komori::tt::CircularEntryPointer{begin + 1, begin, end};
    p_back_minus_1_ = komori::tt::CircularEntryPointer{back - 1, begin, end};
    p_back_ = komori::tt::CircularEntryPointer{back, begin, end};
  }

  std::array<komori::tt::Entry, 10> vals_;
  komori::tt::CircularEntryPointer p_begin_;
  komori::tt::CircularEntryPointer p_begin_plus_1_;
  komori::tt::CircularEntryPointer p_back_minus_1_;
  komori::tt::CircularEntryPointer p_back_;
};
}  // namespace

TEST_F(CircularEntryPointerTest, Increment) {
  EXPECT_EQ(&*(++p_begin_), &*p_begin_plus_1_);
  --p_begin_;
  EXPECT_EQ(&*(++p_back_minus_1_), &*p_back_);
  --p_back_minus_1_;
  EXPECT_EQ(&*(++p_back_), &*p_begin_);
}

TEST_F(CircularEntryPointerTest, Decrement) {
  EXPECT_EQ(&*(--p_begin_), &*p_back_);
  ++p_begin_;
  EXPECT_EQ(&*(--p_begin_plus_1_), &*p_begin_);
  EXPECT_EQ(&*(--p_back_), &*p_back_minus_1_);
}

TEST_F(CircularEntryPointerTest, Access) {
  EXPECT_EQ(&p_begin_.operator*(), p_begin_.operator->());

  const auto& cp_begin = p_begin_;
  EXPECT_EQ(&cp_begin.operator*(), cp_begin.operator->());
}

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
  const auto removal_ratio = 0.5;

  komori::SearchAmount i = 1;
  for (auto&& entry : tt_) {
    entry.Init(0x334, HAND_ZERO);
    entry.UpdateUnknown(0, 3, 3, i++, komori::BitSet64::Full(), 334, HAND_ZERO);
  }

  EXPECT_EQ(tt_.CalculateHashRate(), 1.0);
  tt_.CollectGarbage(removal_ratio);
  EXPECT_LT(tt_.CalculateHashRate(), 1.0 - removal_ratio + 0.1);
  EXPECT_GT(tt_.CalculateHashRate(), 1.0 - removal_ratio - 0.1);
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

TEST_F(RegularTableTest, Capacity) {
  EXPECT_EQ(tt_.Capacity(), 2604);
}
