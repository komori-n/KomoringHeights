#include <gtest/gtest.h>

#include "../typedefs.hpp"

using komori::Clamp;
using komori::Constraints;
using komori::DefineComparisonOperatorsByEqualAndLess;
using komori::DefineNotEqualByEqual;
using komori::Delta;
using komori::Identity;
using komori::kInfinitePnDn;
using komori::Phi;
using komori::StepEffect;
using komori::ToString;

namespace {
template <typename T>
class TypesTest : public ::testing::Test {};
using TypesTestTypes = ::testing::Types<int, komori::NodeTag<false>>;
}  // namespace

TYPED_TEST_SUITE(TypesTest, TypesTestTypes);

TYPED_TEST(TypesTest, Identity) {
  ::testing::StaticAssertTypeEq<TypeParam, typename Identity<TypeParam>::type>();
}

TYPED_TEST(TypesTest, Constraints) {
  ::testing::StaticAssertTypeEq<Constraints<TypeParam>, std::nullptr_t>();
}

namespace {
struct Hoge : DefineNotEqualByEqual<Hoge>, DefineComparisonOperatorsByEqualAndLess<Hoge> {
  int val;

  Hoge(int val) : val{val} {}

  friend bool operator==(const Hoge& lhs, const Hoge& rhs) { return lhs.val == rhs.val; }

  friend bool operator<(const Hoge& lhs, const Hoge& rhs) { return lhs.val < rhs.val; }
};
}  // namespace

TEST(DefineOperators, DefineNotEqualByEqualTest) {
  Hoge a{0}, b{1};

  // not equal
  EXPECT_TRUE(a != b);
  EXPECT_FALSE(a != a);

  // less than or equal to
  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(a <= a);
  EXPECT_FALSE(b <= a);

  // greater than
  EXPECT_TRUE(b > a);
  EXPECT_FALSE(b > b);
  EXPECT_FALSE(a > b);

  // greater than or equal to
  EXPECT_TRUE(b >= a);
  EXPECT_TRUE(b >= b);
  EXPECT_FALSE(a >= b);
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

TEST(StepEffectTest, StepEffectTest) {
  EXPECT_EQ(StepEffect(PAWN, BLACK, SQ_55), pawnEffect(BLACK, SQ_55));
  EXPECT_EQ(StepEffect(LANCE, BLACK, SQ_55), pawnEffect(BLACK, SQ_55));
  EXPECT_EQ(StepEffect(KNIGHT, BLACK, SQ_55), knightEffect(BLACK, SQ_55));
  EXPECT_EQ(StepEffect(SILVER, BLACK, SQ_55), silverEffect(BLACK, SQ_55));
  EXPECT_EQ(StepEffect(GOLD, BLACK, SQ_55), goldEffect(BLACK, SQ_55));
  EXPECT_EQ(StepEffect(PRO_PAWN, BLACK, SQ_55), goldEffect(BLACK, SQ_55));
  EXPECT_EQ(StepEffect(PRO_LANCE, BLACK, SQ_55), goldEffect(BLACK, SQ_55));
  EXPECT_EQ(StepEffect(PRO_KNIGHT, BLACK, SQ_55), goldEffect(BLACK, SQ_55));
  EXPECT_EQ(StepEffect(PRO_SILVER, BLACK, SQ_55), goldEffect(BLACK, SQ_55));
  EXPECT_EQ(StepEffect(KING, BLACK, SQ_55), kingEffect(SQ_55));
  EXPECT_EQ(StepEffect(HORSE, BLACK, SQ_55), kingEffect(SQ_55));
  EXPECT_EQ(StepEffect(DRAGON, BLACK, SQ_55), kingEffect(SQ_55));
  EXPECT_EQ(StepEffect(QUEEN, BLACK, SQ_55), kingEffect(SQ_55));
  EXPECT_EQ(StepEffect(BISHOP, BLACK, SQ_55), bishopStepEffect(SQ_55));
  EXPECT_EQ(StepEffect(ROOK, BLACK, SQ_55), rookStepEffect(SQ_55));
  EXPECT_EQ(StepEffect(NO_PIECE_TYPE, BLACK, SQ_55), Bitboard{});
}