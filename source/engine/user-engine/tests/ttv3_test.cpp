#include <gtest/gtest.h>

#include "../ttv3.hpp"

using komori::ttv3::Entry;

TEST(V3EntryTest, NullCheck) {
  Entry entry;

  entry.SetNull();
  EXPECT_TRUE(entry.IsNull());
}
