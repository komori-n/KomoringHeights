#include "../../types.h"

#include "../../extra/all.h"

#include "komoring_heights.hpp"

#if defined(USER_ENGINE)

namespace {
komori::DfPnSearcher g_searcher;
}  // namespace

// USI拡張コマンド"user"が送られてくるとこの関数が呼び出される。実験に使ってください。
void user_test(Position& pos_, std::istringstream& is) {}

// USIに追加オプションを設定したいときは、この関数を定義すること。
// USI::init()のなかからコールバックされる。
void USI::extra_option(USI::OptionsMap& o) {
  o["DepthLimit"] << Option(0, 0, INT_MAX);
  o["NodesLimit"] << Option(0, 0, INT64_MAX);
}

// 起動時に呼び出される。時間のかからない探索関係の初期化処理はここに書くこと。
void Search::init() {}

// isreadyコマンドの応答中に呼び出される。時間のかかる処理はここに書くこと。
void Search::clear() {
  g_searcher.Init();
  g_searcher.Resize(Options["USI_Hash"]);

  if (auto max_search_node = Options["NodesLimit"]) {
    g_searcher.SetMaxSearchNode(max_search_node);
  }

  if (Depth depth_limit = Options["DepthLimit"]) {
    g_searcher.SetMaxSearchNode(depth_limit);
  }
}

// 探索開始時に呼び出される。
// この関数内で初期化を終わらせ、slaveスレッドを起動してThread::search()を呼び出す。
// そのあとslaveスレッドを終了させ、ベストな指し手を返すこと。
void MainThread::search() {
  // 例)
  //  for (auto th : Threads.slaves) th->start_searching();
  //  Thread::search();
  //  for (auto th : Threads.slaves) th->wait_for_search_finished();

  // 通常の go コマンドで呼ばれたときは resign を返す
  if (Search::Limits.mate == 0) {
    sync_cout << "bestmove resign" << sync_endl;
    return;
  }

  Timer time;
  time.reset();

  std::atomic_bool search_end = false;
  std::atomic_bool search_result = false;
  g_searcher.SetMaxSearchNode(0xffff'ffff'ffff'ffffU);
  auto thread = std::thread([&]() {
    search_result = g_searcher.Search(rootPos, Threads.stop);
    search_end = true;
  });

  auto time_up = [&]() { return Search::Limits.mate && time.elapsed() >= Search::Limits.mate; };
  while (!Threads.stop && !time_up() && !search_end) {
    Tools::sleep(100);
  }
  thread.join();

  if (time_up()) {
    sync_cout << "checkmate timeout" << sync_endl;
  } else if (search_end) {
    if (search_result) {
      auto best_moves = g_searcher.BestMoves(rootPos);
      std::ostringstream oss;
      oss << "checkmate";
      for (const auto& move : best_moves) {
        oss << " " << move;
      }
      sync_cout << oss.str() << sync_endl;
    } else {
      sync_cout << "checkmate nomate" << sync_endl;
    }
  }
}

// 探索本体。並列化している場合、ここがslaveのエントリーポイント。
void Thread::search() {}

#endif  // USER_ENGINE
