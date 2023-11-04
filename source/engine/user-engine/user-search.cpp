#include <cmath>
#include <condition_variable>
#include <mutex>

#include "../../extra/all.h"

#include "komoring_heights.hpp"
#include "path_keys.hpp"
#include "typedefs.hpp"

#if defined(USER_ENGINE)

namespace {
komori::KomoringHeights g_searcher;
komori::EngineOption g_option;
std::atomic_bool g_path_key_init_flag;

komori::NodeState g_search_result = komori::NodeState::kUnknown;

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

void PrintResult(bool is_root_or_node, bool is_mate_search, LoseKind kind, const std::string& pv_moves = "resign") {
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
    // `KomoringHeights::Search()` 内で出力しているはずなので、ここでは何もする必要がない。
  }
}

void ShowCommand(Position& pos, std::istringstream& is) {
  // unimplemented
}

void PvCommand(Position& pos, std::istringstream& /* is */) {
  // unimplemented
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
  komori::EngineOption::Init(o);
}

// 起動時に呼び出される。時間のかからない探索関係の初期化処理はここに書くこと。
void Search::init() {}

// isreadyコマンドの応答中に呼び出される。時間のかかる処理はここに書くこと。
void Search::clear() {
  Threads.main()->wait_for_search_finished();
  Threads.clear();

  if (!g_path_key_init_flag) {
    g_path_key_init_flag = true;
    komori::PathKeyInit();
  }
  g_option.Reload(Options);

#if defined(USE_DEEP_DFPN)
  auto d = g_option.deep_dfpn_d_;
  auto e = g_option.deep_dfpn_e_;
  komori::DeepDfpnInit(d, e);
#endif  // defined(USE_DEEP_DFPN)

  g_searcher.Init(g_option, Threads.size());
}

// 探索開始時に呼び出される。
// この関数内で初期化を終わらせ、slaveスレッドを起動してThread::search()を呼び出す。
// そのあとslaveスレッドを終了させ、ベストな指し手を返すこと。
void MainThread::search() {
  // `go mate` で探索開始したときは true、`go` で探索開始したときは false
  bool is_mate_search = Search::Limits.mate != 0;
  bool is_root_or_node = IsPosOrNode(rootPos);

  g_searcher.NewSearch(rootPos, is_root_or_node);
  Threads.start_searching();
  Thread::search();
  Threads.stop = true;
  Threads.wait_for_search_finished();

  Move best_move = MOVE_NONE;
  if (g_search_result == komori::NodeState::kProven) {
    auto best_moves = g_searcher.BestMoves();
    PrintResult(is_root_or_node, is_mate_search, LoseKind::kMate, komori::ToString(best_moves));

    if (!best_moves.empty()) {
      best_move = best_moves[0];
    }
  } else {
    if (g_search_result == komori::NodeState::kDisproven || g_search_result == komori::NodeState::kRepetition) {
      PrintResult(is_root_or_node, is_mate_search, LoseKind::kNoMate);
    } else {
      PrintResult(is_root_or_node, is_mate_search, LoseKind::kTimeout);
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
  const auto result = g_searcher.Search(id(), rootPos, IsPosOrNode(rootPos));
  if (id() == 0) {
    g_search_result = result;
  }
}

#endif  // USER_ENGINE
