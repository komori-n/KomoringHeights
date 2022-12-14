#include <gtest/gtest.h>

#include "../engine_option.hpp"

using komori::EngineOption;
using komori::PostSearchLevel;
using komori::ScoreCalculationMethod;

TEST(EngineOptionTest, Init) {
  USI::OptionsMap o;
  EngineOption::Init(o);

  EXPECT_NE(o.find("NodesLimit"), o.end());
  EXPECT_NE(o.find("PvInterval"), o.end());
  EXPECT_NE(o.find("RootIsAndNodeIfChecked"), o.end());
  EXPECT_NE(o.find("ScoreCalculation"), o.end());
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
  EXPECT_EQ(op.score_method, ScoreCalculationMethod::kPonanza);
  EXPECT_EQ(op.post_search_level, PostSearchLevel::kMinLength);
  EXPECT_EQ(op.tt_read_path, std::string{});
  EXPECT_EQ(op.tt_write_path, std::string{});
  EXPECT_EQ(op.show_pv_after_mate, true);
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
  EXPECT_EQ(op.score_method, ScoreCalculationMethod::kPonanza);
  EXPECT_EQ(op.post_search_level, PostSearchLevel::kNone);
}
