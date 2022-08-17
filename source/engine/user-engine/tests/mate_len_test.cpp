#include <gtest/gtest.h>

#include <sstream>

#include "../mate_len.hpp"

namespace {
template <typename MateLen>
class MateLenTest : public ::testing::Test {};

using TestTypes = ::testing::Types<komori::MateLen16, komori::MateLen>;
};  // namespace

TYPED_TEST_SUITE(MateLenTest, TestTypes);

TYPED_TEST(MateLenTest, Construct) {
  TypeParam mate_len{33, 4};
  EXPECT_EQ(mate_len.len_plus_1, 33);
  EXPECT_EQ(mate_len.final_hand, 4);
}

TYPED_TEST(MateLenTest, OperatorEqual) {
  TypeParam m1{33, 4};
  TypeParam m2{26, 4};
  TypeParam m3{33, 3};
  EXPECT_TRUE(m1 == m1);
  EXPECT_FALSE(m1 == m2);
  EXPECT_FALSE(m1 == m3);
}

TYPED_TEST(MateLenTest, OperatorLess) {
  TypeParam m1{33, 4};
  TypeParam m2{26, 4};
  TypeParam m3{33, 3};
  EXPECT_FALSE(m1 < m2);
  EXPECT_TRUE(m1 < m3);
}

TYPED_TEST(MateLenTest, OperatorPlus) {
  TypeParam m1{26, 4};
  EXPECT_EQ(m1 + 7, (TypeParam{33, 4}));
  EXPECT_EQ(7 + m1, (TypeParam{33, 4}));
}

TYPED_TEST(MateLenTest, OperatorMinus) {
  TypeParam m1{33, 4};
  EXPECT_EQ(m1 - 7, (TypeParam{26, 4}));
}

TYPED_TEST(MateLenTest, OutputOperator) {
  TypeParam m{34, 4};
  std::ostringstream oss;
  oss << m;
  EXPECT_EQ(oss.str(), "33(4)");
}

TEST(MateLen, Convert16) {
  komori::MateLen16 m1{33, 4};
  komori::MateLen expected{33, 4};
  EXPECT_EQ((komori::MateLen{m1}), expected);
  EXPECT_EQ((komori::MateLen{m1}.To16()), m1);
}

TEST(MateLen, Succ) {
  EXPECT_EQ(komori::Succ({33, 4}), (komori::MateLen{33, 3}));
  EXPECT_EQ(komori::Succ({33, 0}), (komori::MateLen{34, komori::MateLen::kFinalHandMax}));
  EXPECT_EQ(komori::Succ2({33, 4}), (komori::MateLen{33, 3}));
  EXPECT_EQ(komori::Succ2({33, 0}), (komori::MateLen{35, komori::MateLen::kFinalHandMax}));
}

TEST(MateLen, Prec) {
  EXPECT_EQ(komori::Prec({33, 4}), (komori::MateLen{33, 5}));
  EXPECT_EQ(komori::Prec({33, komori::MateLen::kFinalHandMax}), (komori::MateLen{32, 0}));
  EXPECT_EQ(komori::Prec2({33, 4}), (komori::MateLen{33, 5}));
  EXPECT_EQ(komori::Prec2({33, komori::MateLen::kFinalHandMax}), (komori::MateLen{31, 0}));
}
