#include "komoring_heights.hpp"

namespace komori {
namespace {
inline std::uint64_t GcInterval(std::uint64_t hash_mb) {
  const std::uint64_t entry_num = hash_mb * 1024 * 1024 / sizeof(tt::Entry);

  return entry_num / 2 * 3;
}
}  // namespace

namespace detail {
void SearchMonitor::NewSearch(std::uint64_t gc_interval) {
  start_time_ = std::chrono::system_clock::now();
  depth_ = 0;

  tp_hist_.Clear();
  mc_hist_.Clear();
  hist_idx_ = 0;

  move_limit_ = std::numeric_limits<std::uint64_t>::max();
  limit_stack_ = {};

  gc_interval_ = gc_interval;
  ResetNextGc();
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
  output.Set(UsiInfoKey::kSelDepth, depth_);
  output.Set(UsiInfoKey::kTime, time_ms);
  output.Set(UsiInfoKey::kNodes, move_count);
  output.Set(UsiInfoKey::kNps, nps);

  return output;
}

void SearchMonitor::ResetNextGc() {
  next_gc_count_ = MoveCount() + gc_interval_;
}

void SearchMonitor::PushLimit(std::uint64_t move_limit) {
  limit_stack_.push(move_limit_);
  move_limit_ = std::min(move_limit_, move_limit);
}

void SearchMonitor::PopLimit() {
  if (!limit_stack_.empty()) {
    move_limit_ = limit_stack_.top();
    limit_stack_.pop();
  }
}
}  // namespace detail

UsiInfo KomoringHeights::CurrentInfo() const {
  UsiInfo usi_output = monitor_.GetInfo();
  usi_output.Set(UsiInfoKey::kHashfull, tt_.Hashfull());
  usi_output.Set(UsiInfoKey::kScore, score_.ToString());

  return usi_output;
}

NodeState KomoringHeights::Search(const Position& n, bool is_root_or_node) {
  // <初期化>
  tt_.NewSearch();
  monitor_.NewSearch(GcInterval(option_.hash_mb));
  monitor_.PushLimit(option_.nodes_limit);
  best_moves_.clear();
  // </初期化>

  Position& nn = const_cast<Position&>(n);
  Node node{nn, is_root_or_node};

  auto [state, len] = SearchMainLoop(node, is_root_or_node);
  bool proven = (state == NodeState::kProven);

  if (proven) {
    if (best_moves_.size() % 2 != static_cast<int>(is_root_or_node)) {
      sync_cout << "info string Failed to detect PV" << sync_endl;
    }
    return NodeState::kProven;
  } else {
    return NodeState::kDisproven;
  }
}

std::pair<NodeState, MateLen> KomoringHeights::SearchMainLoop(Node& n, bool is_root_or_node) {
  auto node_state{NodeState::kUnknown};
  auto len{kMaxMateLen};

  const int max_loop_cnt = option_.post_search_level == PostSearchLevel::kNone ? 1 : 128;
  for (int i = 0; i < max_loop_cnt; ++i) {
    // `result` が余詰探索による不詰だったとき、後から元の状態（詰み）に戻せるようにしておく
    const auto old_score = score_;
    const auto result = SearchEntry(n, len);
    score_ = Score::Make(option_.score_method, result, is_root_or_node);

    auto info = CurrentInfo();

    if (result.Pn() == 0) {
      // len 以下の手数の詰みが帰ってくるはず
      KOMORI_PRECONDITION(result.Len().Len() <= len.Len());
      best_moves_ = GetMatePath(n, result.Len());

      sync_cout << info << "# " << OrdinalNumber(i + 1) << " result: mate in " << best_moves_.size()
                << "(upper_bound:" << result.Len() << ")" << sync_endl;
      std::ostringstream oss;
      for (const auto move : best_moves_) {
        oss << move << " ";
      }
      info.Set(UsiInfoKey::kPv, oss.str());
      sync_cout << info << sync_endl;
      node_state = NodeState::kProven;
      if (result.Len().Len() <= 1) {
        break;
      }

      len = MateLen::Make(result.Len().Len() - 2, MateLen::kFinalHandMax);
    } else {
      sync_cout << info << "# " << OrdinalNumber(i + 1) << " result: " << result << sync_endl;
      if (result.Dn() == 0 && result.Len() < len) {
        sync_cout << info << "Failed to detect PV" << sync_endl;
      }
      if (node_state == NodeState::kProven) {
        best_moves_ = GetMatePath(n, len + 2);
        len = MateLen::Make(len.Len() + 2, MateLen::kFinalHandMax);
        score_ = old_score;
      }
      break;
    }
  }

  return {node_state, len};
}

SearchResult KomoringHeights::SearchEntry(Node& n, MateLen len) {
  SearchResult result{};
  PnDn thpn = (len == kMaxMateLen) ? 1 : kInfinitePnDn;
  PnDn thdn = (len == kMaxMateLen) ? 1 : kInfinitePnDn;

  expansion_list_.Emplace(tt_, n, len, true);
  while (!monitor_.ShouldStop()) {
    result = SearchImpl(n, thpn, thdn, len, false);
    if (result.IsFinal()) {
      break;
    }

    score_ = Score::Make(option_.score_method, result, n.IsRootOrNode());
    // 反復深化のしきい値を適当に伸ばす
    thpn = Clamp(thpn, 2 * result.Pn(), kInfinitePnDn);
    thdn = Clamp(thdn, 2 * result.Dn(), kInfinitePnDn);
    sync_cout << CurrentInfo() << " " << thpn << " " << thdn << sync_endl;
  }
  expansion_list_.Pop();

  auto query = tt_.BuildQuery(n);
  query.SetResult(result);

  return result;
}

SearchResult KomoringHeights::SearchImpl(Node& n, PnDn thpn, PnDn thdn, MateLen len, bool inc_flag) {
  auto& local_expansion = expansion_list_.Current();
  monitor_.Visit(n.GetDepth());
  PrintIfNeeded(n);

  // 必要があれば TCA による探索延長をしたいので、このタイミングで現局面の pn/dn を取得する。
  auto curr_result = local_expansion.CurrentResult(n);
  // Threshold Controlling Algorithm(TCA).
  // 浅い結果を参照している場合、無限ループになる可能性があるので少しだけ探索を延長する
  inc_flag = inc_flag || local_expansion.DoesHaveOldChild();
  if (inc_flag && !curr_result.IsFinal()) {
    if (curr_result.Pn() < kInfinitePnDn) {
      thpn = Clamp(thpn, curr_result.Pn() + 1);
    }

    if (curr_result.Dn() < kInfinitePnDn) {
      thdn = Clamp(thdn, curr_result.Dn() + 1);
    }
  }

  if (monitor_.ShouldGc()) {
    tt_.CollectGarbage();
    monitor_.ResetNextGc();
  }

  while (!monitor_.ShouldStop() && !(curr_result.Pn() >= thpn || curr_result.Dn() >= thdn)) {
    // local_expansion.BestMove() にしたがい子局面を展開する
    // （curr_result.Pn() > 0 && curr_result.Dn() > 0 なので、BestMove が必ず存在する）
    const auto best_move = local_expansion.BestMove();
    // 現局面で `BestMove` が存在するということは、0 手詰みではない。
    // よって、OR Node では最低 1 手詰、AND Node では最低 2 手詰である。
    const auto min_len =
        n.IsOrNode() ? MateLen::Make(1, MateLen::kFinalHandMax) : MateLen::Make(2, MateLen::kFinalHandMax);
    if (len < min_len) {
      local_expansion.UpdateBestChild(SearchResult::MakeFinal<false>(n.OrHandAfter(best_move), min_len, 1));
      curr_result = local_expansion.CurrentResult(n);
      continue;
    }
    const bool is_first_search = local_expansion.FrontIsFirstVisit();
    const BitSet64 sum_mask = local_expansion.FrontSumMask();
    const auto [child_thpn, child_thdn] = local_expansion.PnDnThresholds(thpn, thdn);

    n.DoMove(best_move);

    // 子局面を展開する。展開した expansion は UndoMove() の直前に忘れずに開放しなければならない。
    auto& child_expansion = expansion_list_.Emplace(tt_, n, len - 1, is_first_search, sum_mask);

    SearchResult child_result;
    if (is_first_search) {
      child_result = child_expansion.CurrentResult(n);
      // 新規局面を展開したので、TCA による探索延長をこれ以上続ける必要はない
      inc_flag = false;

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
  }

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

    SearchEntry(n, len);

    // 子ノードの中から最善っぽい手を選ぶ
    Move best_move = MOVE_NONE;
    MateLen best_len = n.IsOrNode() ? kMaxMateLen : kZeroMateLen;
    for (const auto move : MovePicker{n}) {
      auto query = tt_.BuildChildQuery(n, move.move);
      bool does_have_old_child = false;
      const auto child_result = query.LookUp<false>(does_have_old_child, len - 1);
      if (child_result.Pn() != 0) {
        continue;
      }

      if (n.IsOrNode() && child_result.Len() < best_len) {
        best_move = move.move;
        best_len = child_result.Len();
      } else if (!n.IsOrNode() && child_result.Len() > best_len) {
        best_move = move.move;
        best_len = child_result.Len();
      } else if (child_result.Len() == best_len) {
        bool does_have_old_child = false;
        const auto child_result_prec = query.LookUp<false>(does_have_old_child, len - 3);
        if (child_result_prec.Dn() == 0) {
          // move は厳密に best_len 手詰め。
          best_move = move.move;
        }
      }
    }

    if (best_move == MOVE_NONE) {
      break;
    }

    len = len - 1;
    n.DoMove(best_move);
    best_moves.push_back(best_move);
  }

  RollBack(n, best_moves);
  return best_moves;
}

void KomoringHeights::PrintIfNeeded(const Node& n) {
  if (!print_flag_) {
    return;
  }
  print_flag_ = false;

  auto usi_output = CurrentInfo();
  usi_output.Set(UsiInfoKey::kDepth, n.GetDepth());
#if defined(KEEP_LAST_MOVE)
  const auto moves = n.Pos().moves_from_start();
  usi_output.Set(UsiInfoKey::kPv, moves);
  if (const auto p = moves.find_first_of(' '); p != std::string::npos) {
    const auto best_move = moves.substr(0, p);
    usi_output.Set(UsiInfoKey::kCurrMove, best_move);
  }
#endif

  sync_cout << usi_output << sync_endl;

  monitor_.Tick();
}
}  // namespace komori
