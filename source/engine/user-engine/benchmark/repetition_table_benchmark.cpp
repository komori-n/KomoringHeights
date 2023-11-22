#include <benchmark/benchmark.h>
#include "repetition_table.hpp"

using komori::tt::RepetitionTable;

namespace {
constexpr std::size_t kTableSize = 100000;

void RepetitionTable_Clear(benchmark::State& state) {
  RepetitionTable table{};

  table.Resize(100 * kTableSize);
  for (auto _ : state) {
    table.Clear();
  }
}

void RepetitionTable_Insert(benchmark::State& state) {
  RepetitionTable table{};

  table.Resize(kTableSize);
  const auto collision_threshold = static_cast<int>(std::numeric_limits<int>::max() * 0.2);
  const std::uint64_t collision_key = 0x334334334334ULL;

  for (auto _ : state) {
    for (std::size_t i = 0; i < 10000; ++i) {
      if (rand() < collision_threshold) {
        table.Insert(collision_key, rand(), komori::kZeroMateLen);
      } else {
        table.Insert(rand(), rand(), komori::kZeroMateLen);
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
        table.Insert(collision_key, rand(), komori::kZeroMateLen);
      } else {
        table.Insert(rand(), rand(), komori::kZeroMateLen);
      }
    }
    state.ResumeTiming();

    for (std::size_t i = 0; i < 10000; ++i) {
      if (rand() < collision_threshold) {
        benchmark::DoNotOptimize(table.Contains(collision_key, komori::kZeroMateLen));
      } else {
        benchmark::DoNotOptimize(table.Contains(rand(), komori::kZeroMateLen));
      }
    }
  }
}
}  // namespace

BENCHMARK(RepetitionTable_Clear);
BENCHMARK(RepetitionTable_Insert);
BENCHMARK(RepetitionTable_Contains);
