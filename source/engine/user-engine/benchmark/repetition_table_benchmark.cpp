#include <benchmark/benchmark.h>
#include "repetition_table.hpp"

using komori::tt::RepetitionTable;

namespace {
constexpr std::size_t kTableSize = 100000;

void RepetitionTable_Insert(benchmark::State& state) {
  RepetitionTable table{};

  table.Resize(kTableSize);
  const auto collision_threshold = static_cast<int>(std::numeric_limits<int>::max() * 0.2);
  const std::uint64_t collision_key = 0x334334334334ULL;

  for (auto _ : state) {
    for (std::size_t i = 0; i < 10000; ++i) {
      if (rand() < collision_threshold) {
        table.Insert(collision_key, rand());
      } else {
        table.Insert(rand(), rand());
      }
    }
  }
}

void RepetitionTable_Contains(benchmark::State& state) {
  RepetitionTable table{};

  table.Resize(kTableSize);
  const auto collision_threshold = static_cast<int>(std::numeric_limits<int>::max() * 0.2);
  const std::uint64_t collision_key = 0x334334334334ULL;

  for (auto _ : state) {
    state.PauseTiming();
    for (std::size_t i = 0; i < 10000; ++i) {
      if (rand() < collision_threshold) {
        table.Insert(collision_key, rand());
      } else {
        table.Insert(rand(), rand());
      }
    }
    state.ResumeTiming();

    for (std::size_t i = 0; i < 10000; ++i) {
      if (rand() < collision_threshold) {
        benchmark::DoNotOptimize(table.Contains(collision_key));
      } else {
        benchmark::DoNotOptimize(table.Contains(rand()));
      }
    }
  }
}
}  // namespace

BENCHMARK(RepetitionTable_Insert);
BENCHMARK(RepetitionTable_Contains);
