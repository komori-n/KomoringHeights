#include <gtest/gtest.h>

#include "../score.hpp"

using komori::kMaxMateLen;
using komori::kNullKey;
using komori::MateLen;
using komori::Score;
using komori::ScoreCalculationMethod;
using komori::SearchResult;
using komori::UnknownData;

TEST(ScoreTest, MakeUnknown_None) {
  const SearchResult result = SearchResult::MakeUnknown(33, 4, HAND_ZERO, kMaxMateLen, 264, UnknownData{});

  const auto s1 = Score::Make(ScoreCalculationMethod::kNone, result, true);
  EXPECT_EQ(s1.ToString(), "cp 0");

  const auto s2 = Score::Make(ScoreCalculationMethod::kNone, result, false);
  EXPECT_EQ(s2.ToString(), "cp 0");
}

TEST(ScoreTest, MakeUnknown_Dn) {
  const SearchResult result = SearchResult::MakeUnknown(33, 4, HAND_ZERO, kMaxMateLen, 264, UnknownData{});

  const auto s1 = Score::Make(ScoreCalculationMethod::kDn, result, true);
  EXPECT_EQ(s1.ToString(), "cp 4");

  const auto s2 = Score::Make(ScoreCalculationMethod::kDn, result, false);
  EXPECT_EQ(s2.ToString(), "cp -4");
}

TEST(ScoreTest, MakeUnknown_MinusPn) {
  const SearchResult result = SearchResult::MakeUnknown(33, 4, HAND_ZERO, kMaxMateLen, 264, UnknownData{});

  const auto s1 = Score::Make(ScoreCalculationMethod::kMinusPn, result, true);
  EXPECT_EQ(s1.ToString(), "cp -33");

  const auto s2 = Score::Make(ScoreCalculationMethod::kMinusPn, result, false);
  EXPECT_EQ(s2.ToString(), "cp 33");
}

TEST(ScoreTest, MakeUnknown_Ponanza) {
  const SearchResult result = SearchResult::MakeUnknown(33, 4, HAND_ZERO, kMaxMateLen, 264, UnknownData{});

  const auto s1 = Score::Make(ScoreCalculationMethod::kPonanza, result, true);
  EXPECT_EQ(s1.ToString(), "cp -1266");

  const auto s2 = Score::Make(ScoreCalculationMethod::kPonanza, result, false);
  EXPECT_EQ(s2.ToString(), "cp 1266");
}

TEST(ScoreTest, MakeUnknown_Proven) {
  const SearchResult result = SearchResult::MakeFinal<true>(HAND_ZERO, MateLen::Make(26, 4), 1);

  const auto s1 = Score::Make(ScoreCalculationMethod::kNone, result, true);
  EXPECT_EQ(s1.ToString(), "mate 26");
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kDn, result, true));
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kMinusPn, result, true));
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kPonanza, result, true));

  const auto s2 = Score::Make(ScoreCalculationMethod::kNone, result, false);
  EXPECT_EQ(s2.ToString(), "mate -26");
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kDn, result, false));
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kMinusPn, result, false));
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kPonanza, result, false));
}

TEST(ScoreTest, MakeUnknown_Disproven) {
  const SearchResult result = SearchResult::MakeFinal<false>(HAND_ZERO, MateLen::Make(26, 4), 1);

  const auto s1 = Score::Make(ScoreCalculationMethod::kNone, result, true);
  EXPECT_EQ(s1.ToString(), "mate -26");
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kDn, result, true));
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kMinusPn, result, true));
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kPonanza, result, true));

  const auto s2 = Score::Make(ScoreCalculationMethod::kNone, result, false);
  EXPECT_EQ(s2.ToString(), "mate 26");
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kDn, result, false));
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kMinusPn, result, false));
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kPonanza, result, false));
}

TEST(ScoreTest, MakeUnknown_Repetition) {
  const SearchResult result = SearchResult::MakeFinal<false, true>(HAND_ZERO, MateLen::Make(26, 4), 1);

  const auto s1 = Score::Make(ScoreCalculationMethod::kNone, result, true);
  EXPECT_EQ(s1.ToString(), "mate -26");
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kDn, result, true));
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kMinusPn, result, true));
  EXPECT_EQ(s1, Score::Make(ScoreCalculationMethod::kPonanza, result, true));

  const auto s2 = Score::Make(ScoreCalculationMethod::kNone, result, false);
  EXPECT_EQ(s2.ToString(), "mate 26");
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kDn, result, false));
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kMinusPn, result, false));
  EXPECT_EQ(s2, Score::Make(ScoreCalculationMethod::kPonanza, result, false));
}
