#include "../../types.h"

#include <mutex>

#include "../../extra/all.h"

#include "komoring_heights.hpp"
#include "path_keys.hpp"

#if defined(USER_ENGINE)

namespace {
komori::DfPnSearcher g_searcher;
std::once_flag g_path_key_init_flag;

constexpr int kDisprovenLen = -1;
constexpr int kTimeOutLen = -2;

std::string InfoHeader(bool is_mate_search, int solution_len) {
  std::ostringstream oss;
  if (is_mate_search) {
    if (solution_len == kTimeOutLen) {
      oss << "checkmate timeout";
    } else if (solution_len == kDisprovenLen) {
      oss << "checkmate nomate";
    } else {
      oss << "checkmate ";
    }
  } else {
    oss << "info " << g_searcher.Info(0) << "score mate";
    if (solution_len == kTimeOutLen || solution_len == kDisprovenLen) {
      oss << "-1 pv resign";
    } else {
      oss << solution_len << " pv ";
    }
  }
  return oss.str();
}

}  // namespace

// USI拡張コマンド"user"が送られてくるとこの関数が呼び出される。実験に使ってください。
void user_test(Position& pos_, std::istringstream& is) {}

// USIに追加オプションを設定したいときは、この関数を定義すること。
// USI::init()のなかからコールバックされる。
void USI::extra_option(USI::OptionsMap& o) {
  o["DepthLimit"] << Option(0, 0, INT_MAX);
  o["NodesLimit"] << Option(0, 0, INT64_MAX);
  o["PvInterval"] << Option(1000, 0, 1000000);
}

// 起動時に呼び出される。時間のかからない探索関係の初期化処理はここに書くこと。
void Search::init() {}

// isreadyコマンドの応答中に呼び出される。時間のかかる処理はここに書くこと。
void Search::clear() {
  std::call_once(g_path_key_init_flag, komori::PathKeyInit);
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

  Timer timer;
  timer.reset();

  std::atomic_bool search_end = false;
  std::atomic_bool search_result = false;
  g_searcher.SetMaxSearchNode(0xffff'ffff'ffff'ffffU);
  auto thread = std::thread([&]() {
    search_result = g_searcher.Search(rootPos, Threads.stop);
    search_end = true;
  });

  auto time_up = [&]() { return Search::Limits.mate != 0 && timer.elapsed() >= Search::Limits.mate; };
  TimePoint pv_interval = Options["PvInterval"];
  TimePoint last_pv_out = 0;
  while (!Threads.stop && !time_up() && !search_end) {
    Tools::sleep(100);
    if (pv_interval && timer.elapsed() > last_pv_out + pv_interval) {
      g_searcher.SetPrintFlag();
      last_pv_out = timer.elapsed();
    }
  }
  thread.join();

  bool is_mate_search = Search::Limits.mate != 0;
  if (time_up()) {
    sync_cout << InfoHeader(is_mate_search, kTimeOutLen) << sync_endl;
  } else if (search_end) {
    if (search_result) {
      auto best_moves = g_searcher.BestMoves(rootPos);
      std::ostringstream oss;
      oss << InfoHeader(is_mate_search, best_moves.size());
      for (const auto& move : best_moves) {
        oss << " " << move;
      }
      sync_cout << oss.str() << sync_endl;
    } else {
      sync_cout << InfoHeader(is_mate_search, kDisprovenLen) << sync_endl;
    }
  }

  // 通常の go コマンドで呼ばれたときは resign を返す
  if (Search::Limits.mate == 0) {
    // "go infinite"に対してはstopが送られてくるまで待つ。
    while (!Threads.stop && Search::Limits.infinite) {
      Tools::sleep(1);
    }
    sync_cout << "bestmove resign" << sync_endl;
    return;
  }
}

// 探索本体。並列化している場合、ここがslaveのエントリーポイント。
void Thread::search() {}

#endif  // USER_ENGINE
