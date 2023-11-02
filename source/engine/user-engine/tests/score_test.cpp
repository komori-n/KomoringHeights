#include <gtest/gtest.h>

#include "../score.hpp"

using komori::kDepthMaxMateLen;
using komori::kNullKey;
using komori::MateLen;
using komori::Score;
using komori::ScoreCalculationMethod;
using komori::SearchResult;
using komori::UnknownData;

TEST(ScoreTest, MakeProven) {
  const auto s1 = Score::MakeProven(ScoreCalculationMethod::kNone, 334, true);
  EXPECT_EQ(s1.ToString(), "mate 334");

  const auto s2 = Score::MakeProven(ScoreCalculationMethod::kNone, 334, false);
  EXPECT_EQ(s2.ToString(), "mate -334");
}

TEST(ScoreTest, MakeUnknown_None) {
  const SearchResult result = SearchResult::MakeUnknown(33, 4, kDepthMaxMateLen, 264, UnknownData{});

  const auto s1 = Score::Make(ScoreCalculationMethod::kNone, result, true);
  EXPECT_EQ(s1.ToString(), "cp 0");

  const auto s2 = Score::Make(ScoreCalculationMethod::kNone, result, false);
  EXPECT_EQ(s2.ToString(), "cp 0");
}

TEST(ScoreTest, MakeUnknown_Dn) {
  const SearchResult result = SearchResult::MakeUnknown(33, 4, kDepthMaxMateLen, 264, UnknownData{});

  const auto s1 = Score::Make(ScoreCalculationMethod::kDn, result, true);
  EXPECT_EQ(s1.ToString(), "cp 4");

  const auto s2 = Score::Make(ScoreCalculationMethod::kDn, result, false);
  EXPECT_EQ(s2.ToString(), "cp -4");
}

TEST(ScoreTest, MakeUnknown_MinusPn) {
  const SearchResult result = SearchResult::MakeUnknown(33, 4, kDepthMaxMateLen, 264, UnknownData{});

  const auto s1 = Score::Make(ScoreCalculationMethod::kMinusPn, result, true);
  EXPECT_EQ(s1.ToString(), "cp -33");

  const auto s2 = Score::Make(ScoreCalculationMethod::kMinusPn, result, false);
  EXPECT_EQ(s2.ToString(), "cp 33");
}

TEST(ScoreTest, MakeUnknown_Ponanza) {
  const SearchResult result = SearchResult::MakeUnknown(33, 4, kDepthMaxMateLen, 264, UnknownData{});

  const auto s1 = Score::Make(ScoreCalculationMethod::kPonanza, result, true);
  EXPECT_EQ(s1.ToString(), "cp -1266");

  const auto s2 = Score::Make(ScoreCalculationMethod::kPonanza, result, false);
  EXPECT_EQ(s2.ToString(), "cp 1266");
}

TEST(ScoreTest, MakeUnknown_Proven) {
  const SearchResult result = SearchResult::MakeFinal<true>(HAND_ZERO, MateLen{264}, 1);

  const auto s1 = Score::Make(ScoreCalculationMethod::kNone, result, true);
  EXPECT_EQ(s1.ToString(), "mate 264");
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kDn, result, true));
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kMinusPn, result, true));
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kPonanza, result, true));

  const auto s2 = Score::Make(ScoreCalculationMethod::kNone, result, false);
  EXPECT_EQ(s2.ToString(), "mate -264");
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kDn, result, false));
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kMinusPn, result, false));
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kPonanza, result, false));
}

TEST(ScoreTest, MakeUnknown_Disproven) {
  const SearchResult result = SearchResult::MakeFinal<false>(HAND_ZERO, MateLen{264}, 1);

  const auto s1 = Score::Make(ScoreCalculationMethod::kNone, result, true);
  EXPECT_EQ(s1.ToString(), "mate -264");
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kDn, result, true));
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kMinusPn, result, true));
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kPonanza, result, true));

  const auto s2 = Score::Make(ScoreCalculationMethod::kNone, result, false);
  EXPECT_EQ(s2.ToString(), "mate 264");
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kDn, result, false));
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kMinusPn, result, false));
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kPonanza, result, false));
}

TEST(ScoreTest, MakeUnknown_Repetition) {
  const SearchResult result = SearchResult::MakeRepetition(HAND_ZERO, MateLen{264}, 1, 334);

  const auto s1 = Score::Make(ScoreCalculationMethod::kNone, result, true);
  EXPECT_EQ(s1.ToString(), "mate -264");
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kDn, result, true));
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kMinusPn, result, true));
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kPonanza, result, true));

  const auto s2 = Score::Make(ScoreCalculationMethod::kNone, result, false);
  EXPECT_EQ(s2.ToString(), "mate 264");
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kDn, result, false));
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kMinusPn, result, false));
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kPonanza, result, false));
}

TEST(ScoreTest, IsFinal) {
  const SearchResult r1 = SearchResult::MakeUnknown(33, 4, kDepthMaxMateLen, 264, UnknownData{});
  const auto s1 = Score::Make(ScoreCalculationMethod::kNone, r1, true);
  EXPECT_FALSE(s1.IsFinal());

  const SearchResult r2 = SearchResult::MakeFinal<true>(HAND_ZERO, MateLen{264}, 1);
  const auto s2 = Score::Make(ScoreCalculationMethod::kNone, r2, true);
  EXPECT_TRUE(s2.IsFinal());

  const SearchResult r3 = SearchResult::MakeFinal<false>(HAND_ZERO, MateLen{264}, 1);
  const auto s3 = Score::Make(ScoreCalculationMethod::kNone, r3, true);
  EXPECT_TRUE(s2.IsFinal());
}

TEST(ScoreTest, AddOneIfFinal) {
  const SearchResult r1 = SearchResult::MakeUnknown(33, 4, kDepthMaxMateLen, 264, UnknownData{});
  auto s1 = Score::Make(ScoreCalculationMethod::kDn, r1, true);
  s1.AddOneIfFinal();
  EXPECT_EQ(s1.ToString(), "cp 4");

  const SearchResult r2 = SearchResult::MakeFinal<true>(HAND_ZERO, MateLen{263}, 1);
  auto s2 = Score::Make(ScoreCalculationMethod::kDn, r2, true);
  s2.AddOneIfFinal();
  EXPECT_EQ(s2.ToString(), "mate 264");

  const SearchResult r3 = SearchResult::MakeFinal<false>(HAND_ZERO, MateLen{333}, 1);
  auto s3 = Score::Make(ScoreCalculationMethod::kDn, r3, true);
  s3.AddOneIfFinal();
  EXPECT_EQ(s3.ToString(), "mate -334");
}
