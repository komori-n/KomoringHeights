#include <gtest/gtest.h>

#include "../multi_pv.hpp"
#include "test_lib.hpp"

using komori::MultiPv;

TEST(MultiPv, NewSearch) {
  TestNode n{"4k4/9/4P4/9/9/9/9/9/9 b N2r2b4g4s3n4l17p 1", true};

  MultiPv multi_pv{};
  multi_pv.NewSearch(*n);
  const std::vector<Move> legal_moves{
      make_move(SQ_53, SQ_52, B_PAWN),
      make_move_promote(SQ_53, SQ_52, B_PAWN),
      make_move_drop(KNIGHT, SQ_43, BLACK),
      make_move_drop(KNIGHT, SQ_63, BLACK),
  };
  for (const auto move : legal_moves) {
    EXPECT_EQ(multi_pv[move], std::make_pair(0, USI::move(move))) << move;
  }
}

TEST(MultiPv, Update) {
  TestNode n{"4k4/9/4P4/9/9/9/9/9/9 b N2r2b4g4s3n4l17p 1", true};

  MultiPv multi_pv{};
  multi_pv.NewSearch(*n);
  const auto move1 = make_move(SQ_53, SQ_52, B_PAWN);
  const auto move2 = make_move_promote(SQ_53, SQ_52, B_PAWN);
  const auto move3 = make_move_drop(KNIGHT, SQ_43, BLACK);
  const auto move4 = make_move_drop(KNIGHT, SQ_63, BLACK);

  multi_pv.Update(move1, 334, std::string{"test1"});
  multi_pv.Update(move3, 264, std::string{"test3"});
  EXPECT_EQ(multi_pv[move1], std::make_pair(334, std::string{"test1"}));
  EXPECT_EQ(multi_pv[move2], std::make_pair(0, USI::move(move2)));
  EXPECT_EQ(multi_pv[move3], std::make_pair(264, std::string{"test3"}));
  EXPECT_EQ(multi_pv[move4], std::make_pair(0, USI::move(move4)));
}
