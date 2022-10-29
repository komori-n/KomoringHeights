#include <gtest/gtest.h>

#include <unordered_set>

#include "../ttv3transposition_table.hpp"
#include "test_lib.hpp"

using komori::kDepthMax;
using komori::RepetitionTable;
using komori::ttv3::Cluster;
using komori::ttv3::detail::kGcRemoveElementNum;
using komori::ttv3::detail::kGcThreshold;
using komori::ttv3::detail::kNormalRepetitionRatio;
using komori::ttv3::detail::TranspositionTableImpl;

namespace {
struct QueryMock {
  RepetitionTable& rep_table;
  Cluster cluster;
  Key path_key;
  Key board_key;
  Hand hand;
  Depth depth;
};

class TranspositionTableTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tt_.Resize(1);
    tt_.NewSearch();
  }

  TranspositionTableImpl<QueryMock> tt_;
};
}  // namespace

TEST_F(TranspositionTableTest, Resize) {
  for (std::size_t i = 0; i < 20; ++i) {
    tt_.Resize(i);
    const auto size = reinterpret_cast<std::uintptr_t>(&*tt_.end()) - reinterpret_cast<std::uintptr_t>(&*tt_.begin());
    EXPECT_LE(size / 1024 / 1024, i);
  }
}

TEST_F(TranspositionTableTest, BuildQuery) {
  TestNode test_node{"4k4/9/4G4/9/9/9/9/9/9 b P2r2b3g4s4n4l17p 1", true};
  const auto query = tt_.BuildQuery(*test_node);

  EXPECT_TRUE(query.cluster.head_entry >= &*tt_.begin());
  EXPECT_TRUE(query.cluster.head_entry + Cluster::kSize <= &*tt_.end());
  EXPECT_EQ(query.path_key, test_node->GetPathKey());
  EXPECT_EQ(query.board_key, test_node->Pos().state()->board_key());
  EXPECT_EQ(query.hand, test_node->OrHand());
  EXPECT_EQ(query.depth, test_node->GetDepth());
}

TEST_F(TranspositionTableTest, BuildChildQuery) {
  TestNode test_node{"4k4/4+P4/9/9/9/9/9/9/9 w P2r2b4g4s4n4l16p 1", false};
  const Move move = make_move(SQ_51, SQ_52, W_KING);
  const auto query = tt_.BuildChildQuery(*test_node, move);

  EXPECT_TRUE(query.cluster.head_entry >= &*tt_.begin());
  EXPECT_TRUE(query.cluster.head_entry + Cluster::kSize <= &*tt_.end());
  EXPECT_EQ(query.path_key, test_node->PathKeyAfter(move));
  EXPECT_EQ(query.board_key, test_node->Pos().board_key_after(move));
  EXPECT_EQ(query.hand, test_node->OrHandAfter(move));
  EXPECT_EQ(query.depth, test_node->GetDepth() + 1);
}

TEST_F(TranspositionTableTest, BuildQueryByKey_Normal) {
  const Key board_key = 0x334334334334;
  const Key path_key = 0x264264264264;
  const auto hand = MakeHand<PAWN, LANCE, LANCE>();
  const auto query = tt_.BuildQueryByKey(board_key, hand, path_key);

  EXPECT_TRUE(query.cluster.head_entry >= &*tt_.begin());
  EXPECT_TRUE(query.cluster.head_entry + Cluster::kSize <= &*tt_.end());
  EXPECT_EQ(query.path_key, path_key);
  EXPECT_EQ(query.board_key, board_key);
  EXPECT_EQ(query.hand, hand);
  EXPECT_EQ(query.depth, kDepthMax);
}

TEST_F(TranspositionTableTest, BuildQueryByKey_ClusterHasUniformDistribution) {
  std::size_t cluster_head_num = tt_.end() - tt_.begin() - Cluster::kSize;
  std::unordered_set<std::size_t> cluster_ids;
  for (Key k = 0; k < 2 * cluster_head_num; ++k) {
    Key board_key = (0xffff'ffff / 2 / cluster_head_num) * k;
    const auto query = tt_.BuildQueryByKey(board_key, HAND_ZERO);
    ASSERT_TRUE(query.cluster.head_entry >= &*tt_.begin()) << k;
    ASSERT_TRUE(query.cluster.head_entry + Cluster::kSize <= &*tt_.end()) << k;

    const auto cluster_id = query.cluster.head_entry - &*tt_.begin();
    cluster_ids.insert(cluster_id);
  }
  EXPECT_EQ(cluster_ids.size(), cluster_head_num);
}

TEST_F(TranspositionTableTest, Hashfull_EmptyAfterNewSearch) {
  auto query = tt_.BuildQueryByKey(0, HAND_ZERO, 0);

  query.rep_table.Insert(0x334);
  for (auto itr = query.cluster.head_entry; itr != &*tt_.end(); ++itr) {
    itr->Init(0x334, HAND_ZERO, 0, 1, 1, 1);
  }

  EXPECT_GT(tt_.Hashfull(), 0);
  tt_.NewSearch();
  EXPECT_EQ(tt_.Hashfull(), 0);
}

TEST_F(TranspositionTableTest, Hashfull_Full) {
  // board_key = 0 を渡すことで先頭クラスタが取れるはず
  auto query = tt_.BuildQueryByKey(0, HAND_ZERO, 0);
  ASSERT_EQ(query.cluster.head_entry, &*tt_.begin());

  query.rep_table.Insert(0x334);
  for (auto itr = query.cluster.head_entry; itr != &*tt_.end(); ++itr) {
    itr->Init(0x334, HAND_ZERO, 0, 1, 1, 1);
  }

  // RepetitionTable のハッシュ使用率は仕様がコロコロかわる気がするので生値を持ってくる
  const auto expected_real = kNormalRepetitionRatio + (1.0 - kNormalRepetitionRatio) * query.rep_table.HashRate();
  EXPECT_EQ(tt_.Hashfull(), static_cast<std::int32_t>(1000 * expected_real));
}

TEST_F(TranspositionTableTest, CollectGarbage_DoNothing) {
  // board_key = 0 を渡すことで先頭クラスタが取れるはず
  auto query = tt_.BuildQueryByKey(0, HAND_ZERO, 0);
  ASSERT_EQ(query.cluster.head_entry, &*tt_.begin());

  auto itr = query.cluster.head_entry;
  for (std::size_t i = 0; i < kGcThreshold - 1; ++i) {
    itr[i].Init(0x334, HAND_ZERO, 0, 1, 1, 1);
  }
  tt_.CollectGarbage();
  for (std::size_t i = 0; i < kGcThreshold - 1; ++i) {
    EXPECT_FALSE(itr[i].IsNull()) << i;
  }
}

TEST_F(TranspositionTableTest, CollectGarbage_RemoveEntries_Increasing) {
  // board_key = 0 を渡すことで先頭クラスタが取れるはず
  auto query = tt_.BuildQueryByKey(0, HAND_ZERO, 0);
  ASSERT_EQ(query.cluster.head_entry, &*tt_.begin());

  auto itr = query.cluster.head_entry + 1;
  for (std::size_t i = 0; i < kGcThreshold; ++i) {
    itr[i].Init(0x334, HAND_ZERO, 0, 1, 1, 1 + i);
  }
  tt_.CollectGarbage();
  for (std::size_t i = 0; i < kGcRemoveElementNum; ++i) {
    EXPECT_TRUE(itr[i].IsNull()) << i;
  }
  for (std::size_t i = kGcRemoveElementNum; i < kGcThreshold; ++i) {
    EXPECT_FALSE(itr[i].IsNull()) << i;
  }
}

TEST_F(TranspositionTableTest, CollectGarbage_RemoveEntries_Decreasing) {
  // board_key = 0 を渡すことで先頭クラスタが取れるはず
  auto query = tt_.BuildQueryByKey(0, HAND_ZERO, 0);
  ASSERT_EQ(query.cluster.head_entry, &*tt_.begin());

  auto itr = query.cluster.head_entry + 334;
  for (std::size_t i = 0; i < kGcThreshold; ++i) {
    itr[i].Init(0x334, HAND_ZERO, 0, 1, 1, 1 + kGcThreshold - i);
  }
  tt_.CollectGarbage();
  for (std::size_t i = 0; i < kGcThreshold - kGcRemoveElementNum; ++i) {
    EXPECT_FALSE(itr[i].IsNull()) << i;
  }
  for (std::size_t i = kGcThreshold - kGcRemoveElementNum; i < kGcThreshold; ++i) {
    EXPECT_TRUE(itr[i].IsNull()) << i;
  }
}
