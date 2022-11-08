#include <gtest/gtest.h>

#include "../typedefs.hpp"
#include "test_lib.hpp"

using komori::Clamp;
using komori::Delta;
using komori::kInfinitePnDn;
using komori::OrdinalNumber;
using komori::Phi;
using komori::SaturatedAdd;
using komori::SaturatedMultiply;
using komori::ToString;

namespace {
template <typename T>
class SaturationTest : public ::testing::Test {};
using SaturationTestTypes = ::testing::Types<std::uint8_t,
                                             std::uint16_t,
                                             std::uint32_t,
                                             std::uint64_t,
                                             std::int8_t,
                                             std::int16_t,
                                             std::int32_t,
                                             std::int64_t>;
}  // namespace

TYPED_TEST_SUITE(SaturationTest, SaturationTestTypes);

TYPED_TEST(SaturationTest, SaturatedAdd) {
  constexpr TypeParam kMin = std::numeric_limits<TypeParam>::min();
  constexpr TypeParam kMax = std::numeric_limits<TypeParam>::max();

  EXPECT_EQ(SaturatedAdd<TypeParam>(33, 4), 33 + 4);
  EXPECT_EQ(SaturatedAdd<TypeParam>(kMax, 1), kMax);

  if constexpr (std::is_signed_v<TypeParam>) {
    EXPECT_EQ(SaturatedAdd<TypeParam>(-33, -4), -33 - 4);
    EXPECT_EQ(SaturatedAdd<TypeParam>(kMin, kMax), kMin + kMax);
    EXPECT_EQ(SaturatedAdd<TypeParam>(kMax, kMin), kMax + kMin);
    EXPECT_EQ(SaturatedAdd<TypeParam>(kMin, -1), kMin);
  }
}

TYPED_TEST(SaturationTest, SaturatedMultiply) {
  constexpr TypeParam kMin = std::numeric_limits<TypeParam>::min();
  constexpr TypeParam kMax = std::numeric_limits<TypeParam>::max();

  // 調子に乗って (33, 4) を渡すと int8_t のときにオーバーフローするので注意（一敗）
  EXPECT_EQ(SaturatedMultiply<TypeParam>(3, 4), 3 * 4);
  EXPECT_EQ(SaturatedMultiply<TypeParam>(0, 4), 0);
  EXPECT_EQ(SaturatedMultiply<TypeParam>(kMax / 2, 3), kMax);

  if constexpr (std::is_signed_v<TypeParam>) {
    EXPECT_EQ(SaturatedMultiply<TypeParam>(-3, -4), (-3) * (-4));
    EXPECT_EQ(SaturatedMultiply<TypeParam>(3, -4), 3 * (-4));
    EXPECT_EQ(SaturatedMultiply<TypeParam>(-3, 4), (-3) * 4);
    EXPECT_EQ(SaturatedMultiply<TypeParam>(kMin / 2, 3), kMin);
    EXPECT_EQ(SaturatedMultiply<TypeParam>(3, kMin / 2), kMin);
    EXPECT_EQ(SaturatedMultiply<TypeParam>(kMin / 2, -3), kMax);
  }
}

TEST(PnDnTest, ClampTest) {
  EXPECT_EQ(Clamp(10, 5, 20), 10);
  EXPECT_EQ(Clamp(4, 5, 20), 5);
  EXPECT_EQ(Clamp(334, 5, 20), 20);
}

TEST(PnDnTest, PhiTest) {
  EXPECT_EQ(Phi(33, 4, true), 33);
  EXPECT_EQ(Phi(33, 4, false), 4);
}

TEST(PnDnTest, DeltaTest) {
  EXPECT_EQ(Delta(33, 4, true), 4);
  EXPECT_EQ(Delta(33, 4, false), 33);
}

TEST(PnDnTest, ToString) {
  EXPECT_EQ(ToString(kInfinitePnDn), "inf");
  EXPECT_EQ(ToString(kInfinitePnDn + 1), "invalid");
  EXPECT_EQ(ToString(334), "334");
}

TEST(OrdinalNumberTest, All) {
  EXPECT_EQ(OrdinalNumber(1), "1st");
  EXPECT_EQ(OrdinalNumber(2), "2nd");
  EXPECT_EQ(OrdinalNumber(3), "3rd");
  EXPECT_EQ(OrdinalNumber(4), "4th");
  EXPECT_EQ(OrdinalNumber(5), "5th");
  EXPECT_EQ(OrdinalNumber(10), "10th");
  EXPECT_EQ(OrdinalNumber(11), "11th");
  EXPECT_EQ(OrdinalNumber(12), "12th");
  EXPECT_EQ(OrdinalNumber(13), "13th");
  EXPECT_EQ(OrdinalNumber(14), "14th");
  EXPECT_EQ(OrdinalNumber(20), "20th");
  EXPECT_EQ(OrdinalNumber(21), "21st");
  EXPECT_EQ(OrdinalNumber(22), "22nd");
  EXPECT_EQ(OrdinalNumber(23), "23rd");
  EXPECT_EQ(OrdinalNumber(24), "24th");
  EXPECT_EQ(OrdinalNumber(100), "100th");
  EXPECT_EQ(OrdinalNumber(101), "101st");
  EXPECT_EQ(OrdinalNumber(102), "102nd");
  EXPECT_EQ(OrdinalNumber(103), "103rd");
  EXPECT_EQ(OrdinalNumber(104), "104th");
  EXPECT_EQ(OrdinalNumber(111), "111th");
  EXPECT_EQ(OrdinalNumber(112), "112th");
  EXPECT_EQ(OrdinalNumber(113), "113th");
  EXPECT_EQ(OrdinalNumber(120), "120th");
  EXPECT_EQ(OrdinalNumber(121), "121st");
  EXPECT_EQ(OrdinalNumber(122), "122nd");
  EXPECT_EQ(OrdinalNumber(123), "123rd");
  EXPECT_EQ(OrdinalNumber(124), "124th");
}

TEST(DoesHaveMatePossibilityTest, BoardPiece) {
  TestNode node{"4k4/9/4P4/PPPP1PPPP/9/9/9/9/9 b 2r2b4g4s4n4l9p 1", true};
  EXPECT_TRUE(komori::DoesHaveMatePossibility(node->Pos()));

  TestNode node2{"4k4/9/9/PPPPPPPPP/9/9/9/9/9 b 2r2b4g4s4n4l9p 1", true};
  EXPECT_FALSE(komori::DoesHaveMatePossibility(node2->Pos()));
}

TEST(DoesHaveMatePossibilityTest, DoublePawnCheck) {
  TestNode node{"4k4/9/9/9/9/9/9/9/9 b P2r2b4g4s4n4l17p 1", true};
  EXPECT_TRUE(komori::DoesHaveMatePossibility(node->Pos()));

  TestNode node2{"4k4/9/9/9/9/9/9/9/4P4 b P2r2b4g4s4n4l16p 1", true};
  EXPECT_FALSE(komori::DoesHaveMatePossibility(node2->Pos()));
}
