#include "new_kh.hpp"

namespace komori {
namespace {
inline std::uint64_t GcInterval(std::uint64_t hash_mb) {
  const std::uint64_t entry_num = hash_mb * 1024 * 1024 / sizeof(tt::detail::Entry);

  return entry_num / 2 * 3;
}

Score MakeScore(const tt::SearchResult& result, bool root_is_or_node) {
  // unimplemented
  return {};
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
  output.Set(UsiInfo::KeyKind::kSelDepth, depth_)
      .Set(UsiInfo::KeyKind::kTime, time_ms)
      .Set(UsiInfo::KeyKind::kNodes, move_count)
      .Set(UsiInfo::KeyKind::kNps, nps);

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
  usi_output.Set(UsiInfo::KeyKind::kHashfull, tt_.Hashfull()).Set(UsiInfo::KeyKind::kScore, score_);

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

  bool proven = false;
  MateLen len = kMaxMateLen;
  for (int i = 0; i < 10; ++i) {
    const auto result = SearchEntry(node, len, kInfinitePnDn, kInfinitePnDn);
    auto info = CurrentInfo();
    sync_cout << info << " string " << len << " " << result << sync_endl;

    if (result.pn == 0) {
      if (result.len > len) {
        sync_cout << "info string Failed to detect PV" << sync_endl;
        break;
      }
      proven = true;
      len = Prec(result.len);
    } else {
      if (result.dn == 0 && result.len > len) {
        sync_cout << "info string Failed to detect PV" << sync_endl;
      }
      break;
    }
  }

  if (proven) {
    len = Succ2(len);
    bool retry = false;
    while (len.len_plus_1 > 1) {
      const auto [move, hand] = detail::CheckMate1Ply(node);
      if (move != MOVE_NONE) {
        best_moves_.push_back(move);
        node.DoMove(move);
        break;
      }

      Move best_move = MOVE_NONE;
      MateLen best_len = node.IsOrNode() ? kMaxMateLen : kZeroMateLen;
      for (const auto move : MovePicker{node}) {
        auto query = tt_.BuildChildQuery(node, move.move);
        const auto child_result = query.LookUp(len - 1, false);
        if (child_result.pn == 0 && ((node.IsOrNode() && child_result.len + 1 < best_len) ||
                                     (!node.IsOrNode() && child_result.len + 1 > best_len))) {
          best_move = move.move;
          best_len = child_result.len + 1;
        }
      }

      if (best_move == MOVE_NONE || best_len > len) {
        if (!retry) {
          auto res = SearchEntry(node, len, kInfinitePnDn, kInfinitePnDn);
          sync_cout << "info string ex: " << res << sync_endl;
          retry = true;
          continue;
        }
      }
      retry = false;
      sync_cout << "info string " << node.GetDepth() << " " << best_move << " " << best_len << sync_endl;

      if (best_move == MOVE_NONE) {
        break;
      }

      len = len - 1;
      node.DoMove(best_move);
      best_moves_.push_back(best_move);
    }

    RollBack(node, best_moves_);

    if (best_moves_.size() % 2 != static_cast<int>(is_root_or_node)) {
      sync_cout << "info string Failed to detect PV" << sync_endl;
    }
    return NodeState::kProven;
  } else {
    return NodeState::kDisproven;
  }
}

tt::SearchResult KomoringHeights::SearchEntry(Node& n, MateLen len, PnDn thpn, PnDn thdn) {
  ChildrenCache cache{tt_, n, len, true};
  auto result = SearchImpl(n, thpn, thdn, len, cache, false);

  auto query = tt_.BuildQuery(n);
  query.SetResult(result);

  return result;
}

tt::SearchResult KomoringHeights::SearchImpl(Node& n,
                                             PnDn thpn,
                                             PnDn thdn,
                                             MateLen len,
                                             ChildrenCache& cache,
                                             bool inc_flag) {
  monitor_.Visit(n.GetDepth());
  PrintIfNeeded(n);

  // 深さ制限。これ以上探索を続けても詰みが見つかる見込みがないのでここで early return する。
  if (n.IsExceedLimit(option_.depth_limit)) {
    return {kInfinitePnDn, 0, n.OrHand(), len, 1, tt::FinalData{true}};
  }

  // 必要があれば TCA による探索延長をしたいので、このタイミングで現局面の pn/dn を取得する。
  auto curr_result = cache.CurrentResult(n);
  // Threshold Controlling Algorithm(TCA).
  // 浅い結果を参照している場合、無限ループになる可能性があるので少しだけ探索を延長する
  inc_flag = inc_flag || cache.DoesHaveOldChild();
  if (inc_flag && !curr_result.IsFinal()) {
    if (curr_result.pn < kInfinitePnDn) {
      thpn = Clamp(thpn, curr_result.pn + 1);
    }

    if (curr_result.dn < kInfinitePnDn) {
      thdn = Clamp(thdn, curr_result.dn + 1);
    }
  }

  if (monitor_.ShouldGc()) {
    tt_.CollectGarbage();
    monitor_.ResetNextGc();
  }

  while (!monitor_.ShouldStop() && !(curr_result.pn >= thpn || curr_result.dn >= thdn)) {
    // cache.BestMove() にしたがい子局面を展開する
    // （curr_result.Pn() > 0 && curr_result.Dn() > 0 なので、BestMove が必ず存在する）
    const auto best_move = cache.BestMove();
    const auto min_len =
        n.IsOrNode() ? MateLen{2, CountHand(n.OrHandAfter(best_move)) + 1} : MateLen{3, CountHand(n.OrHand()) + 1};
    if (len < min_len) {
      cache.UpdateBestChild({kInfinitePnDn, 0, n.OrHandAfter(best_move), Prec(min_len), 1, tt::FinalData{false}});
      curr_result = cache.CurrentResult(n);
      continue;
    }
    const bool is_first_search = cache.FrontIsFirstVisit();
    const BitSet64 sum_mask = cache.FrontSumMask();
    const auto [child_thpn, child_thdn] = cache.PnDnThresholds(thpn, thdn);

    n.DoMove(best_move);

    // ChildrenCache をローカル変数として持つとスタックが枯渇する。v0.4.1時点では
    //     sizeof(ChildrenCache) == 10832
    // なので、ミクロコスモスを解く場合、スタック領域が 16.5 MB 必要になる。スマホや低スペックPCでも動作するように
    // したいので、ChildrenCache は動的メモリにより確保する。
    //
    // 確保したメモリは UndoMove する直前で忘れずに解放しなければならない。
    auto& child_cache = children_cache_.emplace(tt_, n, len - 1, is_first_search, sum_mask, &cache);

    tt::SearchResult child_result;
    if (is_first_search) {
      child_result = child_cache.CurrentResult(n);
      // 新規局面を展開したので、TCA による探索延長をこれ以上続ける必要はない
      inc_flag = false;

      // 子局面を初展開する場合、child_result を計算した時点で threshold を超過する可能性がある
      // しかし、SearchImpl をコールしてしまうと TCA の探索延長によりすぐに返ってこない可能性がある
      // ゆえに、この時点で Exceed している場合は SearchImpl を呼ばないようにする。
      if (child_result.pn >= child_thpn || child_result.dn >= child_thdn) {
        goto CHILD_SEARCH_END;
      }
    }
    child_result = SearchImpl(n, child_thpn, child_thdn, len - 1, child_cache, inc_flag);

  CHILD_SEARCH_END:
    // 動的に確保した ChildrenCache の領域を忘れずに開放する
    children_cache_.pop();
    n.UndoMove(best_move);

    cache.UpdateBestChild(child_result);
    curr_result = cache.CurrentResult(n);
  }

  return curr_result;
}

void KomoringHeights::PrintIfNeeded(const Node& n) {
  if (!print_flag_) {
    return;
  }
  print_flag_ = false;

  auto usi_output = CurrentInfo();

  usi_output.Set(UsiInfo::KeyKind::kDepth, n.GetDepth());
#if defined(KEEP_LAST_MOVE)
  usi_output.Set(UsiInfo::KeyKind::kPv, n.Pos().moves_from_start());
#endif

  sync_cout << usi_output << sync_endl;

  monitor_.Tick();
}
}  // namespace komori