#include "komoring_heights.hpp"

#include <fstream>
#include <regex>

#include "../../usi.h"

namespace komori {
namespace {
/// GC で削除するエントリの割合
constexpr double kGcRemovalRatio = 0.5;
static_assert(kGcRemovalRatio > 0 && kGcRemovalRatio < 1.0, "kGcRemovalRatio must be greater than 0 and less than 1");

/// GC をするタイミング。置換表使用率がこの値を超えていたら GC を行う。
constexpr double kExecuteGcHashRate = 0.6;
static_assert(kExecuteGcHashRate > 0 && kExecuteGcHashRate < 1.0,
              "kExecuteGcHashRate must be greater than 0 and less than 1");
/// GC をする Hashfull のしきい値
constexpr int kExecuteGcHashfullThreshold = std::max(static_cast<int>(1000 * kExecuteGcHashRate), 1);

constexpr std::uint64_t HashfullCheckInterval(std::uint64_t capacity) noexcept {
  return static_cast<std::uint64_t>(capacity * (1.0 - kExecuteGcHashRate));
}

std::pair<Move, MateLen> LookUpBestMove(tt::TranspositionTable& tt, Node& n, MateLen len) {
  Move best_move = MOVE_NONE;
  MateLen best_len = n.IsOrNode() ? kDepthMaxMateLen : kZeroMateLen;
  MateLen best_disproven_len = kZeroMateLen;
  for (const auto move : MovePicker{n}) {
    const auto query = tt.BuildChildQuery(n, move.move);
    const auto [disproven_len, proven_len] = query.FinalRange();
    if (n.IsOrNode() && proven_len < best_len) {
      best_move = move.move;
      best_len = proven_len;
      best_disproven_len = disproven_len;
    } else if (!n.IsOrNode()) {
      if (proven_len > best_len || (proven_len == best_len && best_disproven_len < disproven_len)) {
        best_move = move.move;
        best_len = proven_len;
        best_disproven_len = disproven_len;
      }
    }
  }

  if (len - 1 < best_len) {
    best_move = MOVE_NONE;
  }

  return {best_move, best_len};
}
}  // namespace

namespace detail {
void SearchMonitor::NewSearch(std::uint64_t hashfull_check_interval, std::uint64_t move_limit) {
  // この時点で stop_ が true ならすぐにやめなければならないので、stop_ の初期化はしない

  start_time_ = std::chrono::system_clock::now();
  max_depth_ = 0;

  tp_hist_.Clear();
  mc_hist_.Clear();
  hist_idx_ = 0;

  move_limit_ = move_limit;
  hashfull_check_interval_ = hashfull_check_interval;
  ResetNextHashfullCheck();
}

void SearchMonitor::Tick() {
  tp_hist_[hist_idx_] = std::chrono::system_clock::now();
  mc_hist_[hist_idx_] = MoveCount();
  hist_idx_++;
}

UsiInfo SearchMonitor::GetInfo() const {
  auto curr_time = std::chrono::system_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - start_time_).count();

  const auto move_count = MoveCount();
  std::uint64_t nps = 0;
  if (hist_idx_ >= kHistLen) {
    const auto tp = tp_hist_[hist_idx_];
    const auto mc = mc_hist_[hist_idx_];
    const auto tp_diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - tp).count();
    nps = (move_count - mc) * 1000ULL / tp_diff;
  } else {
    if (time_ms > 0) {
      nps = move_count * 1000ULL / time_ms;
    }
  }

  UsiInfo output;
  output.Set(UsiInfoKey::kSelDepth, max_depth_);
  output.Set(UsiInfoKey::kTime, time_ms);
  output.Set(UsiInfoKey::kNodes, move_count);
  output.Set(UsiInfoKey::kNps, nps);

  return output;
}

void SearchMonitor::ResetNextHashfullCheck() {
  next_hashfull_check_ = MoveCount() + hashfull_check_interval_;
}
}  // namespace detail

void KomoringHeights::Init(const EngineOption& option, Thread* thread) {
  option_ = option;
  tt_.Resize(option_.hash_mb);
  monitor_.SetThread(thread);

#if defined(USE_TT_SAVE_AND_LOAD)
  const auto& tt_read_path = option_.tt_read_path;
  if (!tt_read_path.empty()) {
    std::ifstream ifs(tt_read_path, std::ios::binary);
    if (ifs) {
      sync_cout << "info string load_path: " << tt_read_path << sync_endl;
      tt_.Load(ifs);
    }
  }
#endif  // defined(USE_TT_SAVE_AND_LOAD)
}

void KomoringHeights::Clear() {
  tt_.Clear();
}

UsiInfo KomoringHeights::CurrentInfo() const {
  UsiInfo usi_output = monitor_.GetInfo();
  usi_output.Set(UsiInfoKey::kHashfull, tt_.Hashfull());

  return usi_output;
}

NodeState KomoringHeights::Search(const Position& n, bool is_root_or_node) {
  // <初期化>
  tt_.NewSearch();
  monitor_.NewSearch(HashfullCheckInterval(tt_.Capacity()), option_.nodes_limit);
  best_moves_.clear();
  after_final_ = false;

  if (tt_.Hashfull() >= kExecuteGcHashfullThreshold) {
    tt_.CollectGarbage(kGcRemovalRatio);
  }
  // </初期化>

  auto& nn = const_cast<Position&>(n);
  Node node{nn, is_root_or_node};

  auto [state, len] = SearchMainLoop(node);

#if defined(USE_TT_SAVE_AND_LOAD)
  const auto tt_write_path = option_.tt_write_path;
  if (!tt_write_path.empty()) {
    std::ofstream ofs(tt_write_path, std::ios::binary);
    if (ofs) {
      sync_cout << "info string save_path: " << tt_write_path << sync_endl;
      tt_.Save(ofs);
    }
  }
#endif  // defined(USE_TT_SAVE_AND_LOAD)

  if (state == NodeState::kProven) {
    if (best_moves_.size() % 2 != static_cast<int>(is_root_or_node)) {
      sync_cout << "info string Failed to detect PV" << sync_endl;
    }
  }

  return state;
}

std::pair<NodeState, MateLen> KomoringHeights::SearchMainLoop(Node& n) {
  NodeState node_state = NodeState::kUnknown;
  auto len{kDepthMaxMateLen};

  for (Depth i = 0; i < kDepthMax; ++i) {
    const auto result = SearchEntry(n, len);
    const auto score = Score::Make(option_.score_method, result, n.IsRootOrNode());

    auto info = CurrentInfo();
    info.Set(UsiInfoKey::kScore, score.ToString());

    if (result.Pn() == 0) {
      // len 以下の手数の詰みが帰ってくるはず
      KOMORI_PRECONDITION(result.Len().Len() <= len.Len());
      best_moves_ = GetMatePath(n, result.Len());

      if (!option_.disable_info_print) {
        sync_cout << info << "# " << OrdinalNumber(i + 1) << " result: mate in " << best_moves_.size()
                  << "(upper_bound:" << result.Len() << ")" << sync_endl;
        std::ostringstream oss;
        for (const auto move : best_moves_) {
          oss << move << " ";
        }
        info.Set(UsiInfoKey::kPv, oss.str());
        sync_cout << info << sync_endl;
      }

      node_state = NodeState::kProven;
      if (result.Len().Len() <= 1) {
        break;
      }

      if (option_.post_search_level == PostSearchLevel::kNone ||
          (option_.post_search_level == PostSearchLevel::kUpperBound && best_moves_.size() == result.Len().Len())) {
        break;
      }

      len = result.Len() - 2;
      after_final_ = true;
    } else {
      if (!option_.disable_info_print) {
        sync_cout << info << "# " << OrdinalNumber(i + 1) << " result: " << result << sync_endl;
        if (result.Dn() == 0 && result.Len() < len) {
          sync_cout << info << "Failed to detect PV" << sync_endl;
        }
      }

      if (node_state == NodeState::kUnknown) {
        node_state = result.GetNodeState();
      } else if (node_state == NodeState::kProven) {
        len = len + 2;
        best_moves_ = GetMatePath(n, len);
      }
      break;
    }
  }

  return {node_state, len};
}

SearchResult KomoringHeights::SearchEntry(Node& n, MateLen len) {
  SearchResult result{};
  PnDn thpn = (len == kDepthMaxMateLen) ? 1 : kInfinitePnDn;
  PnDn thdn = (len == kDepthMaxMateLen) ? 1 : kInfinitePnDn;

  expansion_list_.Emplace(tt_, n, len, true);
  while (!monitor_.ShouldStop() && thpn <= kInfinitePnDn && thdn <= kInfinitePnDn) {
    std::uint32_t inc_flag = 0;
    result = SearchImpl(n, thpn, thdn, len, inc_flag);
    if (result.IsFinal()) {
      break;
    }

    if (result.Pn() >= kInfinitePnDn || result.Dn() >= kInfinitePnDn) {
      auto info = CurrentInfo();
      sync_cout << info << "error: " << (result.Pn() >= kInfinitePnDn ? "pn" : "dn") << " overflow detected"
                << sync_endl;
      break;
    }

    // 反復深化のしきい値を適当に伸ばす
    thpn = ClampPnDn(thpn, SaturatedMultiply<PnDn>(result.Pn(), 2), kInfinitePnDn);
    thdn = ClampPnDn(thdn, SaturatedMultiply<PnDn>(result.Dn(), 2), kInfinitePnDn);
  }
  expansion_list_.Pop();

  auto query = tt_.BuildQuery(n);
  query.SetResult(result);

  return result;
}

SearchResult KomoringHeights::SearchImpl(Node& n, PnDn thpn, PnDn thdn, MateLen len, std::uint32_t& inc_flag) {
  const PnDn orig_thpn = thpn;
  const PnDn orig_thdn = thdn;
  const std::uint32_t orig_inc_flag = inc_flag;

  auto& local_expansion = expansion_list_.Current();
  monitor_.Visit(n.GetDepth());
  PrintIfNeeded(n);

  if (n.GetDepth() >= kDepthMax) {
    return SearchResult::MakeRepetition(n.OrHand(), len, 1, 0);
  }

  expansion_list_.EliminateDoubleCount(tt_, n);

  // 必要があれば TCA による探索延長をしたいので、このタイミングで現局面の pn/dn を取得する。
  auto curr_result = local_expansion.CurrentResult(n);
  // Threshold Controlling Algorithm(TCA).
  // 浅い結果を参照している場合、無限ループになる可能性があるので少しだけ探索を延長する
  if (local_expansion.DoesHaveOldChild()) {
    inc_flag++;
  }

  if (inc_flag > 0) {
    ExtendSearchThreshold(curr_result, thpn, thdn);
  }

  if (n.GetDepth() > 0 && monitor_.ShouldCheckHashfull()) {
    if (tt_.Hashfull() >= kExecuteGcHashfullThreshold) {
      tt_.CollectGarbage(kGcRemovalRatio);
    }
    monitor_.ResetNextHashfullCheck();
  }

  while (!monitor_.ShouldStop() && (curr_result.Pn() < thpn && curr_result.Dn() < thdn)) {
    // local_expansion.BestMove() にしたがい子局面を展開する
    // （curr_result.Pn() > 0 && curr_result.Dn() > 0 なので、BestMove が必ず存在する）
    const auto best_move = local_expansion.BestMove();
    const bool is_first_search = local_expansion.FrontIsFirstVisit();
    const BitSet64 sum_mask = local_expansion.FrontSumMask();
    const auto [child_thpn, child_thdn] = local_expansion.FrontPnDnThresholds(thpn, thdn);

    n.DoMove(best_move);

    // 子局面を展開する。展開した expansion は UndoMove() の直前に忘れずに開放しなければならない。
    auto& child_expansion = expansion_list_.Emplace(tt_, n, len - 1, is_first_search, sum_mask);

    SearchResult child_result;
    if (is_first_search) {
      child_result = child_expansion.CurrentResult(n);
      // 新規局面を展開したので、inc_flag を 1 つ減らしておく
      // オリジナルの TCA では inc_flag は bool 値なので単に false を代入していたが、KomoringHeights では
      // 非負整数へ拡張している。
      if (inc_flag > 0) {
        inc_flag--;
      }

      // 子局面を初展開する場合、child_result を計算した時点で threshold を超過する可能性がある
      // しかし、SearchImpl をコールしてしまうと TCA の探索延長によりすぐに返ってこない可能性がある
      // ゆえに、この時点で Exceed している場合は SearchImpl を呼ばないようにする。
      if (child_result.Pn() >= child_thpn || child_result.Dn() >= child_thdn) {
        goto CHILD_SEARCH_END;
      }
    }
    child_result = SearchImpl(n, child_thpn, child_thdn, len - 1, inc_flag);

  CHILD_SEARCH_END:
    expansion_list_.Pop();
    n.UndoMove();

    local_expansion.UpdateBestChild(child_result);
    curr_result = local_expansion.CurrentResult(n);

    // TCA で延長したしきい値はいったん戻す
    thpn = orig_thpn;
    thdn = orig_thdn;
    if (inc_flag > 0) {
      // TCA 継続中ならしきい値を伸ばす
      ExtendSearchThreshold(curr_result, thpn, thdn);
    } else if (inc_flag == 0 && orig_inc_flag > 0) {
      // TCA の展開が終わったので、いったん親局面に戻る
      break;
    }
  }

  /// `inc_flag` の値は探索前より小さくなっているはず
  inc_flag = std::min(inc_flag, orig_inc_flag);
  return curr_result;
}

std::vector<Move> KomoringHeights::GetMatePath(Node& n, MateLen len) {
  std::vector<Move> best_moves;
  while (len.Len() > 0) {
    // 1手詰はTTに書かれていない可能性があるので先にチェックする
    const auto [move, hand] = CheckMate1Ply(n);
    if (move != MOVE_NONE) {
      best_moves.push_back(move);
      n.DoMove(move);
      break;
    }

    // 子ノードの中から最善っぽい手を選ぶ
    auto [best_move, new_len] = LookUpBestMove(tt_, n, len);
    if (best_move == MOVE_NONE) {
      tt_.NewSearch();
      SearchEntry(n, len);

      std::tie(best_move, new_len) = LookUpBestMove(tt_, n, len);
      if (best_move == MOVE_NONE) {
        break;
      }
    }

    len = new_len;
    n.DoMove(best_move);
    best_moves.push_back(best_move);
  }

  RollBack(n, best_moves);
  return best_moves;
}

void KomoringHeights::PrintIfNeeded(const Node& n) {
  if (!print_flag_.exchange(false, std::memory_order_relaxed)) {
    return;
  }

  auto usi_output = CurrentInfo();
  usi_output.Set(UsiInfoKey::kDepth, n.GetDepth());
  if (!after_final_ || option_.show_pv_after_mate) {
    if (!expansion_list_.IsEmpty()) {
      const auto& root = expansion_list_.Root();
      if (!root.empty()) {
        const auto root_best_move = root.BestMove();
        const auto root_best_result = root.BestResult();
        const auto score = Score::Make(option_.score_method, root_best_result, n.IsRootOrNode());

        usi_output.Set(UsiInfoKey::kCurrMove, USI::move(root_best_move));
        usi_output.Set(UsiInfoKey::kPv, ToString(n.MovesFromStart()));
        usi_output.Set(UsiInfoKey::kScore, score.ToString());
      }
    }
  }

  sync_cout << usi_output << sync_endl;

  monitor_.Tick();
}
}  // namespace komori
