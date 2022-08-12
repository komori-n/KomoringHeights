#include <gtest/gtest.h>

#include "new_cc.hpp"

TEST(IndexTable, Push) {
  komori::detail::IndexTable idx;

  EXPECT_EQ(idx.Push(2), 0);
  EXPECT_EQ(idx.Push(6), 1);
  EXPECT_EQ(idx.Push(4), 2);
}

TEST(IndexTable, Pop) {
  komori::detail::IndexTable idx;

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);

  EXPECT_EQ(idx.size(), 3);
  idx.Pop();
  EXPECT_EQ(idx.size(), 2);
}

TEST(IndexTable, operator) {
  komori::detail::IndexTable idx;

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);

  EXPECT_EQ(idx[0], 2);
  EXPECT_EQ(idx[1], 6);
  EXPECT_EQ(idx[2], 4);
}

TEST(IndexTable, iterators) {
  komori::detail::IndexTable idx;

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);
  EXPECT_EQ(*idx.begin(), 2);
  EXPECT_EQ(idx.begin() + 3, idx.end());
  EXPECT_EQ(*idx.begin(), idx.front());
}

TEST(IndexTable, size) {
  komori::detail::IndexTable idx;
  EXPECT_TRUE(idx.empty());
  EXPECT_EQ(idx.size(), 0);

  idx.Push(2);
  idx.Push(6);
  idx.Push(4);
  EXPECT_FALSE(idx.empty());
  EXPECT_EQ(idx.size(), 3);
}