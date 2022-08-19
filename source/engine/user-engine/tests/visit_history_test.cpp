#include <gtest/gtest.h>

#include "../../../thread.h"
#include "../visit_history.hpp"
#include "test_lib.hpp"

using komori::VisitHistory;

namespace {
class VisitHistoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    hand_p1_ = MakeHand<PAWN>();
    hand_p2_ = MakeHand<PAWN, PAWN>();
  }

  Hand hand_p1_, hand_p2_;
};
}  // namespace

TEST_F(VisitHistoryTest, Visit) {
  VisitHistory visit_history;

  EXPECT_FALSE(visit_history.Contains(334, hand_p1_));
  EXPECT_FALSE(visit_history.Contains(334));
  EXPECT_FALSE(visit_history.IsInferior(334, HAND_ZERO));
  EXPECT_FALSE(visit_history.IsSuperior(334, hand_p2_));

  visit_history.Visit(334, hand_p1_);

  EXPECT_TRUE(visit_history.Contains(334, hand_p1_));
  EXPECT_TRUE(visit_history.Contains(334));
  EXPECT_TRUE(visit_history.IsInferior(334, HAND_ZERO));
  EXPECT_TRUE(visit_history.IsSuperior(334, hand_p2_));
  EXPECT_FALSE(visit_history.Contains(334, HAND_ZERO));
  EXPECT_FALSE(visit_history.IsInferior(334, hand_p2_));
  EXPECT_FALSE(visit_history.IsSuperior(334, HAND_ZERO));
}

TEST_F(VisitHistoryTest, Leave) {
  VisitHistory visit_history;
  visit_history.Visit(334, HAND_ZERO);
  visit_history.Visit(334, hand_p1_);
  visit_history.Visit(334, hand_p2_);

  EXPECT_TRUE(visit_history.Contains(334, hand_p1_));

  visit_history.Leave(334, hand_p1_);

  EXPECT_FALSE(visit_history.Contains(334, hand_p1_));
}