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
komori::EngineOption g_option;
std::once_flag g_path_key_init_flag;

std::atomic_bool g_search_end = false;
komori::NodeState g_search_result = komori::NodeState::kNullState;

/// 局面が OR node っぽいかどうかを調べる。困ったら OR node として処理する。
bool IsPosOrNode(const Position& root_pos) {
  Color us = root_pos.side_to_move();
  Color them = ~us;

  if (root_pos.king_square(us) == SQ_NB) {
    return true;
  } else if (root_pos.king_square(them) == SQ_NB) {
    return false;
  }

  if (root_pos.in_check() && g_option.root_is_and_node_if_checked) {
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
    auto usi_output = g_searcher.CurrentInfo();
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
  TimePoint pv_interval = g_option.pv_interval;
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
      g_searcher.RequestPrint();
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
  g_option.Init(o);
}

// 起動時に呼び出される。時間のかからない探索関係の初期化処理はここに書くこと。
void Search::init() {}

// isreadyコマンドの応答中に呼び出される。時間のかかる処理はここに書くこと。
void Search::clear() {
  std::call_once(g_path_key_init_flag, komori::PathKeyInit);
  g_option.Reload(Options);

#if defined(USE_DEEP_DFPN)
  auto d = g_option.deep_dfpn_d_;
  auto e = g_option.deep_dfpn_e_;
  komori::DeepDfpnInit(d, e);
#endif  // defined(USE_DEEP_DFPN)

  g_searcher.Init(g_option, Threads.main());
}

// 探索開始時に呼び出される。
// この関数内で初期化を終わらせ、slaveスレッドを起動してThread::search()を呼び出す。
// そのあとslaveスレッドを終了させ、ベストな指し手を返すこと。
void MainThread::search() {
  // `go mate` で探索開始したときは true、`go` で探索開始したときは false
  bool is_mate_search = Search::Limits.mate != 0;
  bool is_root_or_node = IsPosOrNode(rootPos);

  g_searcher.ResetStop();

  g_search_end = false;
  // thread が 2 つ以上使える場合、main thread ではない方をタイマースレッドとして使いたい
  if (Threads.size() > 1) {
    Threads[1]->start_searching();

    g_search_result = g_searcher.Search(rootPos, is_root_or_node);
    g_search_end = true;

    Threads[1]->wait_for_search_finished();
  } else {
    // thread が 1 つしか使いない場合、しれっと thread を起動してタイマー役をしてもらう
    std::thread th{[this]() { Thread::search(); }};

    g_search_result = g_searcher.Search(rootPos, is_root_or_node);
    g_search_end = true;

    th.join();
  }

  Move best_move = MOVE_NONE;
  if (g_search_result == komori::NodeState::kProvenState) {
    auto best_moves = g_searcher.BestMoves();
    std::ostringstream oss;
    for (const auto& move : best_moves) {
      oss << move << " ";
    }
    PrintResult(is_mate_search, LoseKind::kMate, oss.str());

    if (!best_moves.empty()) {
      best_move = best_moves[0];
    }
  } else {
    if (g_search_result == komori::NodeState::kDisprovenState ||
        g_search_result == komori::NodeState::kRepetitionState) {
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
    if (best_move == MOVE_NONE) {
      sync_cout << "bestmove resign" << sync_endl;
    } else {
      sync_cout << "bestmove " << best_move << sync_endl;
    }
    return;
  }
}

// 探索本体。並列化している場合、ここがslaveのエントリーポイント。
void Thread::search() {
  WaitSearchEnd(g_search_end);
  g_searcher.SetStop();
}

#endif  // USER_ENGINE
