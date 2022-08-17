#include <gtest/gtest.h>

#include <cstdint>

#include "../circular_array.hpp"

using komori::CircularArray;

TEST(CircularArrayTest, Operator) {
  CircularArray<std::uint32_t, 3> ca;

  ca[0] = 2;
  ca[1] = 6;
  ca[2] = 4;
  EXPECT_EQ(ca[0], 2);
  EXPECT_EQ(ca[1], 6);
  EXPECT_EQ(ca[2], 4);
  EXPECT_EQ(ca[3], 2);

  const auto& const_ca = ca;
  EXPECT_EQ(const_ca[0], 2);
  EXPECT_EQ(const_ca[1], 6);
  EXPECT_EQ(const_ca[2], 4);
  EXPECT_EQ(const_ca[3], 2);
}

TEST(CircularArrayTest, Clear) {
  CircularArray<std::uint32_t, 3> ca;
  for (std::size_t i = 0; i < 3; ++i) {
    ca[i] = i + 10;
    EXPECT_EQ(ca[i], i + 10);
  }

  ca.Clear();
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(ca[i], 0);
  }
}