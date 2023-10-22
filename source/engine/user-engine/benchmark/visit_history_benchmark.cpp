#include <benchmark/benchmark.h>

#include <random>
#include <vector>

#include "visit_history.hpp"

using komori::VisitHistory;

namespace {
void VisitHistory_RandomVisitLeave(benchmark::State& state) {
  struct Query {
    Key board_key;
    Hand hand;
    Depth depth;
  };

  std::mt19937 mt(334);
  std::uniform_int_distribution<Key> key_dist;
  std::vector<Query> queries;
  for (int i = 0; i < 50; ++i) {
    queries.push_back({key_dist(mt), HAND_ZERO, i});
  }

  VisitHistory visit_history{};
  for (auto _ : state) {
    for (auto itr = queries.begin(); itr != queries.end(); ++itr) {
      const auto [board, hand, depth] = *itr;
      visit_history.Visit(board, hand, depth);
    }

    for (auto itr = queries.rbegin(); itr != queries.rend(); ++itr) {
      const auto [board, hand, depth] = *itr;
      visit_history.Leave(board, hand, depth);
    }
  }
}

void VisitHistory_FixedVisitLeave(benchmark::State& state) {
  VisitHistory visit_history{};
  for (auto _ : state) {
    for (std::int32_t i = 0; i < 50; ++i) {
      visit_history.Visit(0x334334ULL, Hand(i), Depth{i});
    }

    for (std::int32_t i = 50 - 1; i >= 0; --i) {
      visit_history.Leave(0x334334ULL, Hand(i), Depth{i});
    }
  }
}

void VisitHistory_Contains(benchmark::State& state) {
  VisitHistory visit_history{};
  for (std::int32_t i = 0; i < 50; ++i) {
    visit_history.Visit(0x334334ULL, Hand(i), Depth{i});
  }

  for (auto _ : state) {
    for (std::int32_t i = 0; i < 50; ++i) {
      benchmark::DoNotOptimize(visit_history.Contains(0x334334ULL, Hand(i)));
    }
  }
}
}  // namespace

BENCHMARK(VisitHistory_RandomVisitLeave);
BENCHMARK(VisitHistory_FixedVisitLeave);
BENCHMARK(VisitHistory_Contains);
