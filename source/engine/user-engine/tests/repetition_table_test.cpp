#include <gtest/gtest.h>

#include "../repetition_table.hpp"

using komori::tt::RepetitionTable;

TEST(RepetitionTable, Constructor) {
  RepetitionTable rep1{};
  EXPECT_EQ(rep1.Size(), 1);

  RepetitionTable rep2(334);
  EXPECT_EQ(rep2.Size(), 334);
}

TEST(RepetitionTable, Resize) {
  RepetitionTable rep_table{334};
  rep_table.Insert(33, 4);
  EXPECT_EQ(rep_table.Contains(33), std::optional<Depth>{4});

  rep_table.Resize(334);  // no effect
  EXPECT_EQ(rep_table.Contains(33), std::optional<Depth>{4});

  rep_table.Resize(264);  // should erase Entry{33, 4}
  EXPECT_FALSE(rep_table.Contains(33));
}

TEST(RepetitionTable, Clear) {
  RepetitionTable rep_table;

  rep_table.Insert(334, 264);
  EXPECT_TRUE(rep_table.Contains(334));
  rep_table.Clear();
  EXPECT_FALSE(rep_table.Contains(334));
}

TEST(RepetitionTable, Insert) {
  RepetitionTable rep_table(3340);

  EXPECT_FALSE(rep_table.Contains(334));
  rep_table.Insert(334, 264);
  EXPECT_EQ(rep_table.Contains(334), std::optional<Depth>{264});
  rep_table.Insert(334, 334);
  EXPECT_EQ(rep_table.Contains(334), std::optional<Depth>{334});
  rep_table.Insert(334, 264);
  EXPECT_EQ(rep_table.Contains(334), std::optional<Depth>{334});

  EXPECT_FALSE(rep_table.Contains(335));
  rep_table.Insert(335, 264);
  EXPECT_EQ(rep_table.Contains(335), std::optional<Depth>{264});
}

TEST(RepetitionTable, InsertBoundary) {
  RepetitionTable rep_table(334);
  const auto max_key = std::numeric_limits<Key>::max();
  rep_table.Insert(max_key, 1);
  EXPECT_EQ(rep_table.Contains(max_key), std::optional<Depth>{1});
  EXPECT_FALSE(rep_table.Contains(max_key - 1));

  rep_table.Insert(max_key - 1, 2);
  EXPECT_EQ(rep_table.Contains(max_key), std::optional<Depth>{1});
  EXPECT_EQ(rep_table.Contains(max_key - 1), std::optional<Depth>{2});
}

TEST(RepetitionTable, GenerationUpdate) {
  RepetitionTable rep_table(334 * 20);
  for (std::size_t i = 0; i < 334; ++i) {
    EXPECT_EQ(rep_table.GetGeneration(), 0) << i;
    rep_table.Insert(i, 0);
  }
  EXPECT_EQ(rep_table.GetGeneration(), 1);
}

TEST(RepetitionTable, HashRate) {
  RepetitionTable rep_table(20);
  for (std::size_t i = 0; i < 6; ++i) {
    EXPECT_NEAR(rep_table.HashRate(), i / 20.0, 0.001) << i;
    rep_table.Insert(i, i);
  }

  EXPECT_NEAR(rep_table.HashRate(), 3 / 20.0, 0.001);
}

TEST(RepetitionTable, CollectGarbageFirstTime) {
  // 1 entry per 1 generation
  RepetitionTable rep_table(20);

  for (std::uint32_t i = 0; i < 6; ++i) {
    rep_table.Insert(i, i);
    EXPECT_EQ(rep_table.GetGeneration(), i + 1);
  }

  // 0, 1, 2 should be erased
  for (std::uint32_t i = 0; i < 3; ++i) {
    EXPECT_FALSE(rep_table.Contains(i));
  }

  // 3, 4, 5 should be kept
  for (std::uint32_t i = 3; i < 6; ++i) {
    EXPECT_EQ(rep_table.Contains(i), std::optional<Depth>{i});
  }
}

TEST(RepetitionTable, CollectGarbageSecondTIme) {
  // 1 entry per 1 generation
  RepetitionTable rep_table(20);

  for (std::uint32_t i = 0; i < 9; ++i) {
    rep_table.Insert(i, i);
    EXPECT_EQ(rep_table.GetGeneration(), i + 1);
  }

  // 0-6 should be erased
  for (std::uint32_t i = 0; i < 6; ++i) {
    EXPECT_FALSE(rep_table.Contains(i));
  }

  // 6, 7, 8 should be kept
  for (std::uint32_t i = 6; i < 9; ++i) {
    EXPECT_EQ(rep_table.Contains(i), std::optional<Depth>{i});
  }
}
