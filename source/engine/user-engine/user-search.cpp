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
bool g_root_is_and_node_if_checked{false};

/// 局面が OR node っぽいかどうかを調べる。困ったら OR node として処理する。
bool IsPosOrNode(const Position& root_pos) {
  Color us = root_pos.side_to_move();
  Color them = ~us;

  if (root_pos.king_square(us) == SQ_NB) {
    return true;
  } else if (root_pos.king_square(them) == SQ_NB) {
    return false;
  }

  if (root_pos.in_check() && g_root_is_and_node_if_checked) {
    return false;
  }
  return true;
}

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

  bool is_root_or_node = IsPosOrNode(pos);
  g_searcher.ShowValues(pos, is_root_or_node, moves);
}

void PvCommand(Position& pos, std::istringstream& /* is */) {
  bool is_root_or_node = IsPosOrNode(pos);
  g_searcher.ShowPv(pos, is_root_or_node);
}

void WaitSearchEnd(const std::atomic_bool& search_end) {
  Timer timer;
  timer.reset();

  bool is_mate_search = Search::Limits.mate != 0;
  auto time_up = [&]() { return is_mate_search && timer.elapsed() >= Search::Limits.mate; };
  TimePoint pv_interval = Options["PvInterval"];
  TimePoint last_pv_out = 0;
  int sleep_cnt = 0;
  while (!Threads.stop && !time_up() && !search_end) {
    ++sleep_cnt;

    if (sleep_cnt < 100) {
      Tools::sleep(2);  // 100ms 寝ると時間がかかるので、探索開始直後は sleep 時間を短くする
    } else {
      Tools::sleep(100);
    }

    if (pv_interval && timer.elapsed() > last_pv_out + pv_interval) {
      g_searcher.SetPrintFlag();
      last_pv_out = timer.elapsed();
    }
  }
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

  o["YozumeNodeCount"] << Option(300, 0, INT_MAX);
  o["YozumePath"] << Option(10000, 0, INT_MAX);

  o["RootIsAndNodeIfChecked"] << Option(false);

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

  g_root_is_and_node_if_checked = Options["RootIsAndNodeIfChecked"];
}

// 探索開始時に呼び出される。
// この関数内で初期化を終わらせ、slaveスレッドを起動してThread::search()を呼び出す。
// そのあとslaveスレッドを終了させ、ベストな指し手を返すこと。
void MainThread::search() {
  // `go mate` で探索開始したときは true、`go` で探索開始したときは false
  bool is_mate_search = Search::Limits.mate != 0;
  bool is_root_or_node = IsPosOrNode(rootPos);

  std::atomic_bool search_end = false;
  komori::NodeState search_result = komori::NodeState::kNullState;
  auto thread = std::thread([&]() {
    search_result = g_searcher.Search(rootPos, is_root_or_node, Threads.stop);
    search_end = true;
  });

  WaitSearchEnd(search_end);
  Threads.stop = true;
  thread.join();

  if (search_result == komori::NodeState::kProvenState) {
    auto best_moves = g_searcher.BestMoves();
    std::ostringstream oss;
    for (const auto& move : best_moves) {
      oss << move << " ";
    }
    PrintResult(is_mate_search, LoseKind::kMate, oss.str());
  } else {
    if (search_result == komori::NodeState::kDisprovenState) {
      PrintResult(is_mate_search, LoseKind::kNoMate);
    } else {
      PrintResult(is_mate_search, LoseKind::kTimeout);
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
