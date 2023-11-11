#include <gtest/gtest.h>

#include "../pv_list.hpp"
#include "test_lib.hpp"

using komori::MateLen;
using komori::PvList;
using komori::SearchResult;
using komori::SearchResultComparer;

TEST(PvList, NewSearch) {
  TestNode n{"4k4/9/4P4/9/9/9/9/9/9 b N2r2b4g4s3n4l17p 1", true};

  PvList pv_list{};
  pv_list.NewSearch(*n);
  const std::vector<Move> legal_moves{
      make_move(SQ_53, SQ_52, B_PAWN),
      make_move_promote(SQ_53, SQ_52, B_PAWN),
      make_move_drop(KNIGHT, SQ_43, BLACK),
      make_move_drop(KNIGHT, SQ_63, BLACK),
  };
  for (const auto move : legal_moves) {
    EXPECT_FALSE(pv_list.IsProven(move)) << move;
  }
}

TEST(PvList, UpdateFirst) {
  TestNode n{"4k4/9/4P4/9/9/9/9/9/9 b N2r2b4g4s3n4l17p 1", true};

  PvList pv_list{};
  pv_list.NewSearch(*n);
  const auto move1 = make_move(SQ_53, SQ_52, B_PAWN);
  const auto move2 = make_move_promote(SQ_53, SQ_52, B_PAWN);
  const auto move3 = make_move_drop(KNIGHT, SQ_43, BLACK);
  const auto move4 = make_move_drop(KNIGHT, SQ_63, BLACK);

  const auto result1 = SearchResult::MakeFirstVisit(33, 4, MateLen{264}, 1);
  const auto result2 = SearchResult::MakeFinal<true>(MakeHand<PAWN, LANCE>(), MateLen{334}, 1);
  const auto result3 = SearchResult::MakeFinal<false>(MakeHand<LANCE, KNIGHT>(), MateLen{334}, 1);
  const auto result4 = SearchResult::MakeRepetition(MakeHand<PAWN, PAWN>(), MateLen{445}, 1, 1);

  pv_list.Update(move1, result1, 334, std::vector{move1, move2});
  pv_list.Update(move2, result2);
  pv_list.Update(move3, result3);
  pv_list.Update(move4, result4);

  const auto& list = pv_list.GetPvList();
  const SearchResultComparer comparer{true};
  EXPECT_EQ(comparer(result2, list[0].result), SearchResultComparer::Ordering::kEquivalent);
  EXPECT_EQ(comparer(result1, list[1].result), SearchResultComparer::Ordering::kEquivalent);
  EXPECT_EQ(list[1].depth, 334);
  EXPECT_EQ(list[1].pv, (std::vector{move1, move2}));
  EXPECT_EQ(comparer(result4, list[2].result), SearchResultComparer::Ordering::kEquivalent);
  EXPECT_EQ(comparer(result3, list[3].result), SearchResultComparer::Ordering::kEquivalent);
  EXPECT_EQ(pv_list.BestMoves()[0], move2);
  EXPECT_FALSE(pv_list.IsProven(move1));
  EXPECT_TRUE(pv_list.IsProven(move2));
  EXPECT_FALSE(pv_list.IsProven(move3));
  EXPECT_FALSE(pv_list.IsProven(move4));
}

TEST(PvList, UpdateSecond) {
  TestNode n{"4k4/9/4P4/9/9/9/9/9/9 b N2r2b4g4s3n4l17p 1", true};

  PvList pv_list{};
  pv_list.NewSearch(*n);
  const auto move1 = make_move(SQ_53, SQ_52, B_PAWN);

  const auto result1 = SearchResult::MakeFirstVisit(33, 4, MateLen{264}, 1);
  const auto result2 = SearchResult::MakeFirstVisit(26, 4, MateLen{334}, 1);
  const auto result3 = SearchResult::MakeFinal<true>(MakeHand<PAWN, LANCE>(), MateLen{334}, 1);

  pv_list.Update(move1, result1);
  const SearchResultComparer comparer{true};
  EXPECT_EQ(comparer(result1, pv_list.GetPvList()[0].result), SearchResultComparer::Ordering::kEquivalent);

  // unknown -> unknown
  pv_list.Update(move1, result2);
  EXPECT_EQ(comparer(result2, pv_list.GetPvList()[0].result), SearchResultComparer::Ordering::kEquivalent);

  // unknown -> final
  pv_list.Update(move1, result3);
  EXPECT_EQ(comparer(result3, pv_list.GetPvList()[0].result), SearchResultComparer::Ordering::kEquivalent);

  // final -> unknown
  pv_list.Update(move1, result2);
  EXPECT_EQ(comparer(result3, pv_list.GetPvList()[0].result), SearchResultComparer::Ordering::kEquivalent);
}
