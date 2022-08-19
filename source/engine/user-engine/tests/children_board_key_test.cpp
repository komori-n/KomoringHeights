#include <gtest/gtest.h>

#include "../children_board_key.hpp"
#include "test_lib.hpp"

using komori::ChildrenBoardKey;

TEST(ChildrenBoardKeyTest, Test) {
  TestNode n{"1pG1B4/Gs+P6/pP7/n1ls5/3k5/nL4+r1b/1+p1p+R4/1S7/2N6 b SP2gn2l11p 1", true};
  ChildrenBoardKey children_board_key{*n, n.MovePicker()};

  for (std::size_t i = 0; i < n.MovePicker().size(); ++i) {
    const auto move = n.MovePicker()[i];
    EXPECT_EQ(children_board_key[i], n->BoardKeyAfter(move)) << move;
  }
}