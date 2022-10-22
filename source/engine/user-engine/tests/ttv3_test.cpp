#include <gtest/gtest.h>

#include "../ttv3.hpp"

using komori::ttv3::Entry;

TEST(V3EntryTest, DefaultConstructedInstanceIsNull) {
  Entry entry;
  EXPECT_TRUE(entry.IsNull());
}
