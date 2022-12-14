#include <gtest/gtest.h>

#include "../bitset.hpp"

using komori::BitSet64;

TEST(BitSet, None) {
  EXPECT_EQ(BitSet64::None().Value(), 0);
}

TEST(BitSet, Full) {
  EXPECT_EQ(BitSet64::Full().Value(), std::numeric_limits<std::uint64_t>::max());
}

TEST(BitSet, Constructors) {
  BitSet64 bs{334};

  EXPECT_EQ(bs.Value(), 334);
  BitSet64 bs_lref = bs;
  EXPECT_EQ(bs_lref.Value(), 334);

  BitSet64 bs_rref = std::move(bs);
  EXPECT_EQ(bs_rref.Value(), 334);
}

TEST(BitSet, Operators) {
  BitSet64 bs{334}, bs_lref{}, bs_rref{};

  bs_lref = bs;
  bs_rref = std::move(bs);

  EXPECT_EQ(bs_lref.Value(), 334);
  EXPECT_EQ(bs_rref.Value(), 334);
}

TEST(BitSet, Equal) {
  BitSet64 bs1{334};
  BitSet64 bs2{264};

  EXPECT_EQ(bs1, bs1);
  EXPECT_NE(bs1, bs2);
}

TEST(BitSet, Set) {
  BitSet64 bs{0};

  EXPECT_FALSE(bs.Test(10));
  bs.Set(10);
  EXPECT_TRUE(bs.Test(10));

  BitSet64 bs2 = bs;
  bs2.Set(334);
  EXPECT_EQ(bs2.Value(), bs.Value());
}

TEST(BitSet, Reset) {
  BitSet64 bs{0};

  bs.Set(10);
  EXPECT_TRUE(bs.Test(10));
  bs.Reset(10);
  EXPECT_FALSE(bs.Test(10));

  BitSet64 bs2 = bs;
  bs2.Reset(334);
  EXPECT_EQ(bs2.Value(), bs.Value());
}

TEST(BitSet, Test) {
  BitSet64 bs{0};

  bs.Set(10);
  EXPECT_TRUE(bs.Test(10));
  EXPECT_TRUE(bs[10]);
  EXPECT_FALSE(bs.Test(334));
  EXPECT_FALSE(bs[334]);
}
