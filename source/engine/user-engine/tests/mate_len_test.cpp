#include <gtest/gtest.h>

#include <sstream>

#include "../mate_len.hpp"

using komori::MateLen;
using komori::MateLen16;

namespace {
template <typename MateLen>
class MateLenTest : public ::testing::Test {};

using TestTypes = ::testing::Types<MateLen16, MateLen>;
};  // namespace

TYPED_TEST_SUITE(MateLenTest, TestTypes);

TYPED_TEST(MateLenTest, Construct) {
  TypeParam mate_len = TypeParam::Make(33, 4);
  EXPECT_EQ(mate_len.Len(), 33);
  EXPECT_EQ(mate_len.FinalHand(), 4);
}

TYPED_TEST(MateLenTest, OperatorEqual) {
  TypeParam m1 = TypeParam::Make(33, 4);
  TypeParam m2 = TypeParam::Make(26, 4);
  TypeParam m3 = TypeParam::Make(33, 3);
  EXPECT_TRUE(m1 == m1);
  EXPECT_FALSE(m1 == m2);
  EXPECT_FALSE(m1 == m3);
}

TYPED_TEST(MateLenTest, OperatorLess) {
  TypeParam m1 = TypeParam::Make(33, 4);
  TypeParam m2 = TypeParam::Make(26, 4);
  TypeParam m3 = TypeParam::Make(33, 3);
  EXPECT_FALSE(m1 < m2);
  EXPECT_TRUE(m1 < m3);
}

TYPED_TEST(MateLenTest, OperatorPlus) {
  TypeParam m1 = TypeParam::Make(26, 4);
  EXPECT_EQ(m1 + 7, (TypeParam::Make(33, 4)));
  EXPECT_EQ(7 + m1, (TypeParam::Make(33, 4)));
}

TYPED_TEST(MateLenTest, OperatorMinus) {
  TypeParam m1 = TypeParam::Make(33, 4);
  EXPECT_EQ(m1 - 7, (TypeParam::Make(26, 4)));
}

TYPED_TEST(MateLenTest, OutputOperator) {
  TypeParam m = TypeParam::Make(33, 4);
  std::ostringstream oss;
  oss << m;
  EXPECT_EQ(oss.str(), "33(4)");
}

TEST(MateLen, Convert16) {
  MateLen16 m1 = MateLen16::Make(33, 4);
  MateLen expected = MateLen::Make(33, 4);
  EXPECT_EQ((MateLen::From(m1)), expected);
  EXPECT_EQ((MateLen::From(m1).To16()), m1);
}

TEST(MateLen, Succ) {
  EXPECT_EQ((MateLen::Make(33, 4).Succ()), (MateLen::Make(33, 3)));
  EXPECT_EQ((MateLen::Make(33, 0).Succ()), (MateLen::Make(34, MateLen::kFinalHandMax)));
  EXPECT_EQ((MateLen::Make(33, 4).Succ2()), (MateLen::Make(33, 3)));
  EXPECT_EQ((MateLen::Make(33, 0).Succ2()), (MateLen::Make(35, MateLen::kFinalHandMax)));
}

TEST(MateLen, Prec) {
  EXPECT_EQ((MateLen::Make(33, 4).Prec()), (MateLen::Make(33, 5)));
  EXPECT_EQ((MateLen::Make(33, MateLen::kFinalHandMax).Prec()), (MateLen::Make(32, 0)));
  EXPECT_EQ((MateLen::Make(33, 4).Prec2()), (MateLen::Make(33, 5)));
  EXPECT_EQ((MateLen::Make(33, MateLen::kFinalHandMax).Prec2()), (MateLen::Make(31, 0)));
}
