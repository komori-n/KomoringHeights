#include <gtest/gtest.h>

#include "../repetition_table.hpp"

using komori::tt::RepetitionTable;

TEST(RepetitionTable, Clear) {
  RepetitionTable rep_table;

  rep_table.Insert(334, 264);
  EXPECT_EQ(rep_table.Size(), 1);
  rep_table.Clear();
  EXPECT_EQ(rep_table.Size(), 0);
}

TEST(RepetitionTable, MaxSize) {
  RepetitionTable rep_table;

  rep_table.SetTableSizeMax(2);
  rep_table.Insert(334, 264);
  rep_table.Insert(264, 264);
  rep_table.Insert(445, 264);
  EXPECT_LE(rep_table.Size(), 2);
}

TEST(RepetitionTable, Insert) {
  RepetitionTable rep_table;

  EXPECT_FALSE(rep_table.Contains(334));
  rep_table.Insert(334, 264);
  EXPECT_EQ(rep_table.Contains(334), std::optional<Depth>{264});
  rep_table.Insert(334, 334);
  EXPECT_EQ(rep_table.Contains(334), std::optional<Depth>{334});
  rep_table.Insert(334, 264);
  EXPECT_EQ(rep_table.Contains(334), std::optional<Depth>{334});
}

TEST(RepetitionTable, HashRate) {
  RepetitionTable rep_table;
  rep_table.SetTableSizeMax(2);
  EXPECT_EQ(rep_table.HashRate(), 0.0);

  rep_table.Insert(334, 264);
  EXPECT_GT(rep_table.HashRate(), 0.0);
}
