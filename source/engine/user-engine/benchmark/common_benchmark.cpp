#include <benchmark/benchmark.h>

#include "../../../extra/all.h"

namespace {
void TimerBenchmark(benchmark::State& state) {
  Timer timer;
  timer.reset();
  for (auto _ : state) {
    for (std::uint64_t i = 0; i < 4096; ++i) {
      benchmark::DoNotOptimize(timer.elapsed());
    }
  }
}

void SparseTimerBenchmark(benchmark::State& state) {
  Timer timer;
  timer.reset();
  std::uint64_t i;
  for (auto _ : state) {
    for (std::uint64_t i = 0; i < 4096; ++i) {
      benchmark::DoNotOptimize(i);
      if (i == 3304) {
        benchmark::DoNotOptimize(timer.elapsed());
      }
    }
  }
}
}  // namespace

BENCHMARK(TimerBenchmark);
BENCHMARK(SparseTimerBenchmark);
