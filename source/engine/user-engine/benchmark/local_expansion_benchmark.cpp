#include <benchmark/benchmark.h>

#include "../tests/test_lib.hpp"
#include "local_expansion.hpp"
#include "transposition_table.hpp"

using komori::kDepthMaxMateLen;
using komori::LocalExpansion;
using komori::SearchResult;
using komori::UnknownData;
using komori::tt::TranspositionTable;

namespace {
void LocalExpansionConstruction(benchmark::State& state) {
  TestNode node{"1pG1B4/Gs+P6/pP7/n1ls5/3k5/nL4+r1b/1+p1p+R4/1S7/2N6 b SP2gn2l11p 1", true};
  TranspositionTable tt;
  tt.Resize(19 * 5 * 5 + 1);
  const auto first_search = false;

  for (auto _ : state) {
    const LocalExpansion local_expansion{tt, *node, kDepthMaxMateLen, first_search};
    benchmark::DoNotOptimize(local_expansion);
  }
}

void LocalExpansionConstruction2(benchmark::State& state) {
  TestNode node{"1pG6/Gs+P6/pP7/n1lsS4/1k6R/n7b/1N+Bp5/1S7/9 w Pr2gn3l12p 14", false};
  TranspositionTable tt;
  tt.Resize(19 * 5 * 5 + 1);
  const auto first_search = state.range() != 0;

  for (auto _ : state) {
    const LocalExpansion local_expansion{tt, *node, kDepthMaxMateLen, first_search};
    benchmark::DoNotOptimize(local_expansion);
  }
}
}  // namespace

BENCHMARK(LocalExpansionConstruction);
BENCHMARK(LocalExpansionConstruction2)->Arg(0)->Arg(1);
