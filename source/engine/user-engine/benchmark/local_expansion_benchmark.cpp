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
  const auto first_search = state.range() != 0;

  for (auto _ : state) {
    const LocalExpansion local_expansion{tt, *node, kDepthMaxMateLen, first_search};
    benchmark::DoNotOptimize(local_expansion);
  }
}

void LocalExpansionConstructionAlmostFull(benchmark::State& state) {
  const auto first_search = state.range() != 0;

  TestNode node{"1pG1B4/Gs+P6/pP7/n1ls5/3k5/nL4+r1b/1+p1p+R4/1S7/2N6 b SP2gn2l11p 1", true};
  TranspositionTable tt;
  tt.Resize(19 * 5 * 5 + 1);
  Hand hand = HAND_ZERO;
  for (int pawn = 0; pawn <= 18; ++pawn) {
    for (int lance = 0; lance <= 4; ++lance) {
      for (int knight = 0; knight <= 4; ++knight) {
        add_hand(hand, PAWN, pawn);
        add_hand(hand, LANCE, lance);
        add_hand(hand, KNIGHT, knight);

        const auto query = tt.BuildQueryByKey({0x01, hand});
        const UnknownData unknown_data{};
        const auto result = SearchResult::MakeUnknown(33, 4, komori::MateLen{1}, 1, unknown_data);
        query.SetResult(result);
      }
    }
  }

  for (auto _ : state) {
    const LocalExpansion local_expansion{tt, *node, kDepthMaxMateLen, first_search};
    benchmark::DoNotOptimize(local_expansion);
  }
}
}  // namespace

BENCHMARK(LocalExpansionConstruction)->Arg(0)->Arg(1);
BENCHMARK(LocalExpansionConstructionAlmostFull)->Arg(0)->Arg(1);
