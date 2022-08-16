#include <gtest/gtest.h>

#include <sstream>

#include "../mate_len.hpp"

TEST(MateLen, Construct) {
  komori::MateLen mate_len{33, 4};
  EXPECT_EQ(mate_len.len_plus_1, 33);
  EXPECT_EQ(mate_len.final_hand, 4);
}

TEST(MateLen, OperatorEqual) {
  komori::MateLen m1{33, 4};
  komori::MateLen m2{26, 4};
  komori::MateLen m3{33, 3};
  EXPECT_TRUE(m1 == m1);
  EXPECT_FALSE(m1 == m2);
  EXPECT_FALSE(m1 == m3);
}

TEST(MateLen, OperatorNotEqual) {
  komori::MateLen m1{33, 4};
  komori::MateLen m2{26, 4};
  komori::MateLen m3{33, 3};
  EXPECT_FALSE(m1 < m2);
  EXPECT_TRUE(m1 < m3);
}

TEST(MateLen, OperatorPlus) {
  komori::MateLen m1{26, 4};
  EXPECT_EQ(m1 + 7, (komori::MateLen{33, 4}));
  EXPECT_EQ(7 + m1, (komori::MateLen{33, 4}));
}

TEST(MateLen, OperatorMinus) {
  komori::MateLen m1{33, 4};
  EXPECT_EQ(m1 - 7, (komori::MateLen{26, 4}));
}

TEST(MateLen, Succ) {
  EXPECT_EQ(komori::Succ({33, 4}), (komori::MateLen{33, 3}));
  EXPECT_EQ(komori::Succ({33, 0}), (komori::MateLen{34, 15}));
}

TEST(MateLen, Prec) {
  EXPECT_EQ(komori::Prec({33, 4}), (komori::MateLen{33, 5}));
  EXPECT_EQ(komori::Prec({33, 15}), (komori::MateLen{32, 0}));
}

TEST(MateLen, OutputOperator) {
  komori::MateLen m{34, 4};
  std::ostringstream oss;
  oss << m;
  EXPECT_EQ(oss.str(), "33(4)");
}