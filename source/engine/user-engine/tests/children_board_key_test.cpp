#include <gtest/gtest.h>

#include "../../../thread.h"
#include "../children_board_key.hpp"

using komori::ChildrenBoardKey;

namespace {
class ChildrenBoardKeyTest : public ::testing::Test {
 protected:
  void Init(const std::string& sfen, bool or_node) {
    p_.set(sfen, &si_, Threads[0]);
    n_ = std::make_unique<komori::Node>(p_, or_node);
    mp_ = std::make_unique<komori::MovePicker>(*n_);
  }

  StateInfo si_;
  Position p_;
  std::unique_ptr<komori::Node> n_;
  std::unique_ptr<komori::MovePicker> mp_;
};
}  // namespace

TEST_F(ChildrenBoardKeyTest, Test) {
  Init("1pG1B4/Gs+P6/pP7/n1ls5/3k5/nL4+r1b/1+p1p+R4/1S7/2N6 b SP2gn2l11p 1", true);
  ChildrenBoardKey children_board_key{*n_, *mp_};

  for (std::size_t i = 0; i < mp_->size(); ++i) {
    const auto move = (*mp_)[i];
    EXPECT_EQ(children_board_key[i], n_->BoardKeyAfter(move)) << move;
  }
}