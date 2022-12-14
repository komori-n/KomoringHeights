#include <gtest/gtest.h>

#include "../fixed_size_stack.hpp"

using komori::FixedSizeStack;

TEST(FixedSizeStackTest, Push) {
  FixedSizeStack<std::uint32_t, 10> idx;

  EXPECT_EQ(idx.Push(2), 0);
  EXPECT_EQ(idx.Push(6), 1);
  EXPECT_EQ(idx.Push(4), 2);
}

TEST(FixedSizeStackTest, Pop) {
  FixedSizeStack<std::uint32_t, 10> idx;

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);

  EXPECT_EQ(idx.size(), 3);
  idx.Pop();
  EXPECT_EQ(idx.size(), 2);
}

TEST(FixedSizeStackTest, operator) {
  FixedSizeStack<std::uint32_t, 10> idx;

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);

  EXPECT_EQ(idx[0], 2);
  EXPECT_EQ(idx[1], 6);
  EXPECT_EQ(idx[2], 4);
}

TEST(FixedSizeStackTest, iterators) {
  FixedSizeStack<std::uint32_t, 10> idx;

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);
  EXPECT_EQ(*idx.begin(), 2);
  EXPECT_EQ(idx.begin() + 3, idx.end());
  EXPECT_EQ(*idx.begin(), idx.front());
  EXPECT_EQ(idx[2], idx.back());
}

TEST(FixedSizeStackTest, size) {
  FixedSizeStack<std::uint32_t, 10> idx;
  EXPECT_TRUE(idx.empty());
  EXPECT_EQ(idx.size(), 0);

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);
  EXPECT_FALSE(idx.empty());
  EXPECT_EQ(idx.size(), 3);
}
