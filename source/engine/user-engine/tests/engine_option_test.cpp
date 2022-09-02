#include <gtest/gtest.h>

#include "../engine_option.hpp"

using komori::EngineOption;

TEST(EngineOptionTest, Init) {
  USI::OptionsMap o;
  EngineOption::Init(o);

  EXPECT_NE(o.find("NodesLimit"), o.end());
  EXPECT_NE(o.find("PvInterval"), o.end());
  EXPECT_NE(o.find("RootIsAndNodeIfChecked"), o.end());
}

TEST(EngineOptionTest, Default) {
  USI::OptionsMap o;
  o["USI_Hash"] << USI::Option(16, 1, 1024, [](const USI::Option&) {});
  o["Threads"] << USI::Option(4, 1, 512, [](const USI::Option&) {});
  EngineOption::Init(o);

  EngineOption op;
  op.Reload(o);

  EXPECT_EQ(op.hash_mb, 16);
  EXPECT_EQ(op.threads, 4);
  EXPECT_EQ(op.nodes_limit, std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(op.pv_interval, 1000);
  EXPECT_EQ(op.root_is_and_node_if_checked, true);
}

TEST(EngineOptionTest, NoInitialization) {
  USI::OptionsMap o;
  EngineOption op;
  op.Reload(o);

  EXPECT_EQ(op.hash_mb, 0);
  EXPECT_EQ(op.threads, 0);
  EXPECT_EQ(op.nodes_limit, std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(op.pv_interval, std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(op.root_is_and_node_if_checked, false);
}
