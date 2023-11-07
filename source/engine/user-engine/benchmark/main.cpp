#include <benchmark/benchmark.h>

#include "../../misc.h"
#include "../../search.h"
#include "../../thread.h"
#include "../../tt.h"
#include "../../usi.h"
#include "path_keys.hpp"
#include "thread_initialization.hpp"

int main(int argc, char** argv) {
  // global variableのconstructorを用いて環境の初期化をすると、Options が未初期化のためsegmentation faultで落ちる（1敗）
  USI::init(Options);
  Bitboards::init();
  Position::init();
  Search::init();
  Threads.set(1);
  komori::PathKeyInit();
  komori::InitializeThread(0, 1);

  char arg0_default[] = "benchmark";
  char* args_default = arg0_default;
  if (!argv) {
    argc = 1;
    argv = &args_default;
  }
  ::benchmark::Initialize(&argc, argv);
  if (::benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;
  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return 0;
}
