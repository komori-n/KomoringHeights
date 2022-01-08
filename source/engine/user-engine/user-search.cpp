#include <cmath>
#include <mutex>

#include "../../extra/all.h"

#include "initial_estimation.hpp"
#include "komoring_heights.hpp"
#include "path_keys.hpp"
#include "typedefs.hpp"

#if defined(USER_ENGINE)

namespace {
komori::KomoringHeights g_searcher;
std::once_flag g_path_key_init_flag;

enum class LoseKind {
  kTimeout,
  kNoMate,
  kMate,
};

void PrintResult(bool is_mate_search, LoseKind kind, const std::string& pv_moves = "resign") {
  if (is_mate_search) {
    switch (kind) {
      case LoseKind::kTimeout:
        sync_cout << "checkmate timeout" << sync_endl;
        break;
      case LoseKind::kNoMate:
        sync_cout << "checkmate nomate" << sync_endl;
        break;
      default:
        sync_cout << "checkmate " << pv_moves << sync_endl;
    }
  } else {
    auto usi_output = g_searcher.Info();
    usi_output.Set(komori::UsiInfo::KeyKind::kDepth, 0).Set(komori::UsiInfo::KeyKind::kPv, pv_moves);
    sync_cout << usi_output << sync_endl;
  }
}

void ShowCommand(Position& pos, std::istringstream& is) {
  std::vector<Move> moves;
  std::vector<StateInfo> st_info;
  std::string token;
  Move m;
  while (is >> token && (m = USI::to_move(pos, token)) != MOVE_NONE) {
    moves.emplace_back(m);
    pos.do_move(m, st_info.emplace_back());
    sync_cout << m << sync_endl;
  }

  sync_cout << pos << sync_endl;

  for (auto itr = moves.crbegin(); itr != moves.crend(); ++itr) {
    pos.undo_move(*itr);
    st_info.pop_back();
  }

  g_searcher.ShowValues(pos, moves);
}

void PvCommand(Position& pos, std::istringstream& /* is */) {
  g_searcher.ShowPv(pos);
}
}  // namespace

// USI拡張コマンド"user"が送られてくるとこの関数が呼び出される。実験に使ってください。
void user_test(Position& pos, std::istringstream& is) {
  std::string cmd;
  is >> cmd;
  if (cmd == "show") {
    ShowCommand(pos, is);
  } else if (cmd == "pv") {
    PvCommand(pos, is);
  }
}

// USIに追加オプションを設定したいときは、この関数を定義すること。
// USI::init()のなかからコールバックされる。
void USI::extra_option(USI::OptionsMap& o) {
  o["DepthLimit"] << Option(0, 0, INT_MAX);
  o["NodesLimit"] << Option(0, 0, INT64_MAX);
  o["PvInterval"] << Option(1000, 0, 1000000);

  o["DebugInfo"] << Option(false, [](bool /*b*/) {});
  o["YozumeNodeCount"] << Option(300, 0, INT_MAX);
  o["YozumePath"] << Option(10000, 0, INT_MAX);

#if defined(USE_DEEP_DFPN)
  o["DeepDfpnPerMile"] << Option(5, 0, 10000);
  o["DeepDfpnMaxVal"] << Option(1000000, 1, INT64_MAX);
#endif  // defined(USE_DEEP_DFPN)
}

// 起動時に呼び出される。時間のかからない探索関係の初期化処理はここに書くこと。
void Search::init() {}

// isreadyコマンドの応答中に呼び出される。時間のかかる処理はここに書くこと。
void Search::clear() {
  std::call_once(g_path_key_init_flag, komori::PathKeyInit);

#if defined(USE_DEEP_DFPN)
  if (auto val = Options["DeepDfpnPerMile"]; static_cast<int>(val) == 0) {
    komori::DeepDfpnInit(0, 1.0);
  } else {
    double e = 0.001 * Options["DeepDfpnPerMile"] + 1.0;
    Depth d = static_cast<Depth>(std::log(static_cast<double>(Options["DeepDfpnMaxVal"])) / std::log(e));
    komori::DeepDfpnInit(d, e);
  }
#endif  // defined(USE_DEEP_DFPN)

  g_searcher.Init();
  g_searcher.Resize(Options["USI_Hash"]);

  if (auto max_search_node = Options["NodesLimit"]) {
    g_searcher.SetMaxSearchNode(max_search_node);
  } else {
    g_searcher.SetMaxSearchNode(0xffff'ffff'ffff'ffffULL);
  }

  if (Depth depth_limit = Options["DepthLimit"]) {
    // n 手詰めを読むためには depth=n+1 まで読む必要がある
    g_searcher.SetMaxDepth(depth_limit);
  }

  if (auto max_yozume_count = Options["YozumeNodeCount"]) {
    g_searcher.SetYozumeCount(max_yozume_count);
  }

  if (auto max_yozume_path = Options["YozumePath"]) {
    g_searcher.SetYozumePath(max_yozume_path);
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

  if (Options["DebugInfo"] != 0) {
    g_searcher.PrintDebugInfo();
  }

  bool is_mate_search = Search::Limits.mate != 0;
  if (time_up()) {
    PrintResult(is_mate_search, LoseKind::kTimeout);
  } else if (search_end) {
    if (search_result) {
      auto best_moves = g_searcher.BestMoves();
      std::ostringstream oss;
      for (const auto& move : best_moves) {
        oss << move << " ";
      }
      PrintResult(is_mate_search, LoseKind::kMate, oss.str());
    } else {
      PrintResult(is_mate_search, LoseKind::kNoMate);
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
