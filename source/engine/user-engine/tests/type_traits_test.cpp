#include <gtest/gtest.h>

#include "../type_traits.hpp"

using komori::Constraints;
using komori::DefineComparisonOperatorsByLess;
using komori::DefineNotEqualByEqual;
using komori::Identity;

namespace {
struct MyTest {};

template <typename T>
class TypesTest : public ::testing::Test {};
using TypesTestTypes = ::testing::Types<int, MyTest>;
}  // namespace

TYPED_TEST_SUITE(TypesTest, TypesTestTypes);

TYPED_TEST(TypesTest, Identity) {
  ::testing::StaticAssertTypeEq<TypeParam, typename Identity<TypeParam>::type>();
}

TYPED_TEST(TypesTest, Constraints) {
  ::testing::StaticAssertTypeEq<Constraints<TypeParam>, std::nullptr_t>();
}

namespace {
struct EqStruct : DefineNotEqualByEqual<EqStruct> {
  std::int32_t val;

  explicit EqStruct(std::int32_t val) : val{val} {}

  friend bool operator==(const EqStruct& lhs, const EqStruct& rhs) { return lhs.val == rhs.val; }
};
}  // namespace

TEST(DefineNotEqualByEqual, Test) {
  EqStruct a{334}, b{264};
  EXPECT_TRUE(a != b);
  EXPECT_FALSE(a != a);
}

namespace {
struct LessStruct : DefineComparisonOperatorsByLess<LessStruct> {
  std::int32_t val;

  explicit LessStruct(std::int32_t val) : val{val} {}

  friend bool operator<(const LessStruct& lhs, const LessStruct& rhs) { return lhs.val < rhs.val; }
};
}  // namespace

TEST(DefineComparisonOperatorsByLess, LessEq) {
  LessStruct a{264}, b{334};
  EXPECT_TRUE(a <= a);
  EXPECT_TRUE(a <= b);
  EXPECT_FALSE(b <= a);
}

TEST(DefineComparisonOperatorsByLess, Greater) {
  LessStruct a{264}, b{334};
  EXPECT_FALSE(a > a);
  EXPECT_FALSE(a > b);
  EXPECT_TRUE(b > a);
}

TEST(DefineComparisonOperatorsByLess, GreaterEq) {
  LessStruct a{264}, b{334};
  EXPECT_TRUE(a >= a);
  EXPECT_FALSE(a >= b);
  EXPECT_TRUE(b >= a);
}
