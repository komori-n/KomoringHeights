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

  EXPECT_EQ(visit_history.Contains(334, hand_p1_), std::nullopt);
  EXPECT_EQ(visit_history.IsInferior(334, HAND_ZERO), std::nullopt);
  EXPECT_EQ(visit_history.IsSuperior(334, hand_p2_), std::nullopt);

  visit_history.Visit(334, hand_p1_, 2);

  EXPECT_EQ(visit_history.Contains(334, hand_p1_), std::optional<Depth>{2});
  EXPECT_EQ(visit_history.IsInferior(334, HAND_ZERO), std::optional<Depth>{2});
  EXPECT_EQ(visit_history.IsSuperior(334, hand_p2_), std::optional<Depth>{2});
  EXPECT_EQ(visit_history.Contains(334, HAND_ZERO), std::nullopt);
  EXPECT_EQ(visit_history.IsInferior(334, hand_p2_), std::nullopt);
  EXPECT_EQ(visit_history.IsSuperior(334, HAND_ZERO), std::nullopt);
}

TEST_F(VisitHistoryTest, Leave) {
  VisitHistory visit_history;
  visit_history.Visit(334, HAND_ZERO, 0);
  visit_history.Visit(334, hand_p1_, 1);
  visit_history.Visit(334, hand_p2_, 2);

  EXPECT_EQ(visit_history.Contains(334, hand_p1_), std::optional<Depth>{1});

  visit_history.Leave(334, hand_p1_, 1);

  EXPECT_EQ(visit_history.Contains(334, hand_p1_), std::nullopt);
}
