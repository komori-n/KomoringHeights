#include <gtest/gtest.h>

#include "../../thread.h"
#include "../move_picker.hpp"

using komori::MovePicker;

namespace {
class MovePickerTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  void Init(const std::string& sfen, bool root_is_or_node) {
    p_.set(sfen, &si_, Threads[0]);
    n_ = std::make_unique<komori::Node>(p_, root_is_or_node);
  }

  Position p_;
  StateInfo si_;
  std::unique_ptr<komori::Node> n_;
};
}  // namespace

TEST_F(MovePickerTest, OrNode_Normal) {
  Init("4k4/9/9/9/9/9/9/9/9 b P2r2b4g4s4n4l17p 1", true);
  MovePicker mp{*n_};

  EXPECT_EQ(mp.size(), 1);
  EXPECT_FALSE(mp.empty());
  const auto move = mp[0];
  EXPECT_EQ(move, make_move_drop(PAWN, SQ_52, BLACK));
}

TEST_F(MovePickerTest, OrNode_InCheck) {
  Init("4k4/3s5/3PK4/9/9/9/9/9/9 b P2r2b4g3s4n4l16p 1", true);
  MovePicker mp{*n_};

  EXPECT_EQ(mp.size(), 1);
  EXPECT_FALSE(mp.empty());
  const auto move = mp[0];
  EXPECT_EQ(move, make_move_promote(SQ_63, SQ_62, B_PAWN));
}

TEST_F(MovePickerTest, OrNode_Empty) {
  Init("lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1", true);
  MovePicker mp{*n_};

  EXPECT_EQ(mp.size(), 0);
  EXPECT_TRUE(mp.empty());
}

TEST_F(MovePickerTest, AndNode) {
  Init("4k4/4+P4/9/9/9/9/9/9/9 w P2r2b4g4s4n4l16p 1", false);
  MovePicker mp{*n_};

  EXPECT_EQ(mp.size(), 1);
  EXPECT_FALSE(mp.empty());
  const auto move = mp[0];
  EXPECT_EQ(move, make_move(SQ_51, SQ_52, W_KING));
}
