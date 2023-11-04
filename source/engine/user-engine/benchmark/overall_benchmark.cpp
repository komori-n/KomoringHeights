#include <benchmark/benchmark.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "komoring_heights.hpp"

using komori::EngineOption;
using komori::KomoringHeights;

namespace {
const std::unordered_map<std::string, std::string> kMateProblems{
    {"mate3-0000000", "ln1gkg1nl/6+P2/2sppps1p/2p3p2/p8/P1P1P3P/2NP1PP2/3s1KSR1/L1+b2G1NL w R2Pbgp 42"},
    {"mate5-0000000", "l2gkg2l/2s3s2/p1nppp1pp/2p3p2/P4P1P1/4n3P/1PPPG1N2/1BKS2+s2/LN3+r3 w RBgl3p 72"}};
StateInfo g_si;

std::unique_ptr<KomoringHeights> MakeEngine() {
  EngineOption option{};
  option.Reload(Options);
  option.pv_interval = 0;
  option.silent = true;

  auto kh = std::make_unique<KomoringHeights>();
  kh->Init(option, 1);

  return kh;
}

std::unique_ptr<Position> GetPosition(std::string problem_name) {
  const auto& sfen = kMateProblems.at(problem_name);

  auto pos = std::make_unique<Position>();
  pos->set(sfen, &g_si, Threads.main());

  return pos;
}

void MateBenchmark(benchmark::State& state) {
  const auto kh = MakeEngine();
  const auto pos = GetPosition(state.name());

  for (auto _ : state) {
    state.PauseTiming();
    kh->Clear();
    kh->NewSearch(*pos, true);
    state.ResumeTiming();

    benchmark::DoNotOptimize(kh->Search(0, *pos, true));
  }
}
}  // namespace

BENCHMARK(MateBenchmark)->Name("mate3-0000000");
BENCHMARK(MateBenchmark)->Name("mate5-0000000");
