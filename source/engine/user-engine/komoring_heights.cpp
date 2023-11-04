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
constexpr double kExecuteGcHashRate = 0.5;
static_assert(kExecuteGcHashRate > 0 && kExecuteGcHashRate < 1.0,
              "kExecuteGcHashRate must be greater than 0 and less than 1");
/// GC をする Hashfull のしきい値
constexpr int kExecuteGcHashfullThreshold = std::max(static_cast<int>(1000 * kExecuteGcHashRate), 1);
/// HashfullCheck をスキップする回数の割合
constexpr std::uint32_t kHashfullCheeckSkipRatio = 4096;

constexpr std::uint64_t HashfullCheckInterval(std::uint64_t capacity) noexcept {
  return static_cast<std::uint64_t>(capacity * (1.0 - kExecuteGcHashRate));
}

// 反復深化のしきい値を適当に伸ばす
constexpr std::pair<PnDn, PnDn> NextPnDnThresholds(std::uint32_t thread_id,
                                                   PnDn pn,
                                                   PnDn dn,
                                                   PnDn curr_thpn,
                                                   PnDn curr_thdn) {
  const auto thpn = static_cast<PnDn>(static_cast<double>(pn) * (1.7 + 0.3 * thread_id));
  const auto thdn = static_cast<PnDn>(static_cast<double>(dn) * (1.7 + 0.3 * thread_id));

  return std::make_pair(ClampPnDn(curr_thpn, thpn, kInfinitePnDn), ClampPnDn(curr_thdn, thdn, kInfinitePnDn));
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
void SearchMonitor::NewSearch(std::uint64_t hashfull_check_interval,
                              std::uint64_t pv_interval,
                              std::uint64_t move_limit) {
  start_time_ = std::chrono::system_clock::now();
  max_depth_ = 0;

  tp_hist_.Clear();
  mc_hist_.Clear();
  hist_idx_ = 0;

  move_limit_ = move_limit;
  if (Search::Limits.mate > 0) {
    time_limit_ = Search::Limits.mate;
  } else if (Search::Limits.movetime > 0) {
    time_limit_ = Search::Limits.movetime;
  } else {
    time_limit_ = std::numeric_limits<TimePoint>::max();
  }

  hashfull_check_interval_ = hashfull_check_interval;
  ResetNextHashfullCheck();

  print_alarm_.Start(pv_interval);
  stop_check_.Start(100);
  stop_.store(false, std::memory_order_release);
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
  output.Set(UsiInfoKey::kSelDepth, max_depth_.load(std::memory_order_relaxed));
  output.Set(UsiInfoKey::kTime, time_ms);
  output.Set(UsiInfoKey::kNodes, move_count);
  output.Set(UsiInfoKey::kNps, nps);

  return output;
}

bool SearchMonitor::ShouldCheckHashfull() {
  hashfull_check_skip_--;
  if (hashfull_check_skip_ > 0) {
    return false;
  }

  hashfull_check_skip_ = kHashfullCheeckSkipRatio;
  return MoveCount() >= next_hashfull_check_;
}

void SearchMonitor::ResetNextHashfullCheck() {
  hashfull_check_skip_ = kHashfullCheeckSkipRatio;
  next_hashfull_check_ = MoveCount() + hashfull_check_interval_;
}

bool SearchMonitor::ShouldStop(std::uint32_t thread_id) {
  const auto stop = stop_.load(std::memory_order_acquire);
  if (thread_id != 0) {
    return stop;
  } else if (stop) {
    // tick 状態に関係なく stop_ なら終了。
    return true;
  } else if (!stop_check_.Tick()) {
    return false;
  }

  // stop_ かどうか改めて判定し直す
  const auto elapsed = Time.elapsed_from_ponderhit();
  stop_.store(MoveCount() >= move_limit_ || elapsed >= time_limit_ || Threads.stop, std::memory_order_release);
  return stop_;
}

bool SearchMonitor::ShouldPrint() {
  if (!print_alarm_.Tick()) {
    return false;
  }
  // print のタイミングで nps も更新しておく
  tp_hist_[hist_idx_] = std::chrono::system_clock::now();
  mc_hist_[hist_idx_] = MoveCount();
  hist_idx_++;

  return true;
}
}  // namespace detail

void KomoringHeights::Init(const EngineOption& option, std::uint32_t num_threads) {
  option_ = option;
  tt_.Resize(option_.hash_mb);
  expansion_list_.resize(num_threads);
  expansion_list_.shrink_to_fit();

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

void KomoringHeights::NewSearch(const Position& n, bool is_root_or_node) {
  auto& nn = const_cast<Position&>(n);
  Node node{nn, is_root_or_node};

  tt_.NewSearch();
  monitor_.NewSearch(HashfullCheckInterval(tt_.Capacity()), option_.pv_interval, option_.nodes_limit);
  best_moves_.clear();
  after_final_ = false;
  score_ = Score{};
  multi_pv_.NewSearch(node);

  if (tt_.Hashfull() >= kExecuteGcHashfullThreshold) {
    tt_.CollectGarbage(kGcRemovalRatio);
  }
}

NodeState KomoringHeights::Search(std::uint32_t thread_id, const Position& n, bool is_root_or_node) {
  auto& nn = const_cast<Position&>(n);
  Node node{nn, is_root_or_node};

  auto [state, len] = SearchMainLoop(thread_id, node);

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

  if (thread_id == 0 && state == NodeState::kProven) {
    if (best_moves_.size() % 2 != static_cast<int>(is_root_or_node)) {
      sync_cout << "info string Failed to detect PV" << sync_endl;
    }
  }

  return state;
}

std::pair<NodeState, MateLen> KomoringHeights::SearchMainLoop(std::uint32_t thread_id, Node& n) {
  NodeState node_state = NodeState::kUnknown;
  auto len{kDepthMaxMateLen};

  for (Depth i = 0; i < kDepthMax; ++i) {
    const auto result = SearchEntry(thread_id, n, len);
    const auto old_score = score_;
    const auto score = Score::Make(option_.score_method, result, n.IsRootOrNode());

    if (thread_id == 0) {
      score_ = score;
    }
    auto info = CurrentInfo();

    if (result.Pn() == 0) {
      // len 以下の手数の詰みが帰ってくるはず
      KOMORI_PRECONDITION(result.Len().Len() <= len.Len());
      if (thread_id == 0) {
        best_moves_ = GetMatePath(n, result.Len());
        multi_pv_.Update(best_moves_[0], 0, ToString(best_moves_));

        if (!option_.silent) {
          sync_cout << CurrentInfo() << "# " << OrdinalNumber(i + 1) << " result: mate in " << best_moves_.size()
                    << "(upper_bound:" << result.Len() << ")" << sync_endl;
          PrintAtRoot(n);
        }
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
      if (thread_id == 0) {
        after_final_ = true;
      }
    } else {
      if (thread_id == 0 && result.Dn() == 0 && result.Len() < len) {
        sync_cout << info << "Failed to detect PV" << sync_endl;
      }

      if (len != kDepthMaxMateLen) {
        len = len + 2;
        if (thread_id == 0) {
          best_moves_ = GetMatePath(n, len);
          multi_pv_.Update(best_moves_[0], 0, ToString(best_moves_));
          score_ = old_score;
        }
      } else {
        node_state = result.GetNodeState();
      }

      if (thread_id == 0 && !option_.silent) {
        sync_cout << info << "# " << OrdinalNumber(i + 1) << " result: " << result << sync_endl;
        PrintAtRoot(n);
      }
      break;
    }
  }

  return {node_state, len};
}

SearchResult KomoringHeights::SearchEntry(std::uint32_t thread_id, Node& n, MateLen len) {
  SearchResult result{};
  PnDn thpn = (len == kDepthMaxMateLen) ? thread_id : kInfinitePnDn;
  PnDn thdn = (len == kDepthMaxMateLen) ? thread_id : kInfinitePnDn;

  expansion_list_[thread_id].Emplace(tt_, n, len, true, BitSet64::Full(), option_.multi_pv);
  while (!monitor_.ShouldStop(thread_id) && thpn <= kInfinitePnDn && thdn <= kInfinitePnDn) {
    result = SearchImplForRoot(thread_id, n, thpn, thdn, len);
    if (result.IsFinal()) {
      break;
    }

    if (thread_id == 0 && (result.Pn() >= kInfinitePnDn || result.Dn() >= kInfinitePnDn)) {
      auto info = CurrentInfo();
      sync_cout << info << "error: " << (result.Pn() >= kInfinitePnDn ? "pn" : "dn") << " overflow detected"
                << sync_endl;
      break;
    }

    std::tie(thpn, thdn) = NextPnDnThresholds(thread_id, result.Pn(), result.Dn(), thpn, thdn);
  }

  auto query = tt_.BuildQuery(n);
  query.SetResult(result);
  expansion_list_[thread_id].Pop();

  return result;
}

SearchResult KomoringHeights::SearchImplForRoot(std::uint32_t thread_id, Node& n, PnDn thpn, PnDn thdn, MateLen len) {
  // 実装内容は SearchImpl() とほぼ同様なので詳しいロジックについてはそちらも参照。

  const auto orig_thpn = thpn;
  const auto orig_thdn = thdn;
  std::uint32_t inc_flag = 0;
  auto& local_expansion = expansion_list_[thread_id].Current();

  if (thread_id == 0 && monitor_.ShouldPrint()) {
    Print(n);
  }
  expansion_list_[thread_id].EliminateDoubleCount(tt_, n);

  auto curr_result = local_expansion.CurrentResult(n);
  if (local_expansion.DoesHaveOldChild()) {
    inc_flag++;
    ExtendSearchThreshold(curr_result, thpn, thdn);
  }

  while (!monitor_.ShouldStop(thread_id) && (curr_result.Pn() < thpn && curr_result.Dn() < thdn)) {
    const auto best_move = local_expansion.BestMove();
    const bool is_first_search = local_expansion.FrontIsFirstVisit();
    const BitSet64 sum_mask = local_expansion.FrontSumMask();
    const auto [child_thpn, child_thdn] = local_expansion.FrontPnDnThresholds(thpn, thdn);

    n.DoMove(best_move);
    auto& child_expansion = expansion_list_[thread_id].Emplace(tt_, n, len - 1, is_first_search, sum_mask);

    SearchResult child_result;
    if (is_first_search) {
      child_result = child_expansion.CurrentResult(n);
      if (inc_flag > 0) {
        inc_flag--;
      }

      if (child_result.Pn() >= child_thpn || child_result.Dn() >= child_thdn) {
        goto CHILD_SEARCH_END;
      }
    }
    child_result = SearchImpl(thread_id, n, child_thpn, child_thdn, len - 1, inc_flag);

  CHILD_SEARCH_END:
    expansion_list_[thread_id].Pop();
    n.UndoMove();

    local_expansion.UpdateBestChild(child_result);
    curr_result = local_expansion.CurrentResult(n);

    if (thread_id == 0 && option_.multi_pv > 1 && len == kDepthMaxMateLen) {
      // Final な手を見つけたとき、multi_pv_ へその手順を記録しておく
      if (child_result.Pn() == 0) {
        n.DoMove(best_move);
        const auto pv = GetMatePath(n, child_result.Len());
        n.UndoMove();
        multi_pv_.Update(best_move, 0, USI::move(best_move) + " " + ToString(pv));
      } else if (child_result.Dn() == 0) {
        n.DoMove(best_move);
        const auto evasion_move = GetEvasion(n);
        n.UndoMove();

        auto pv = USI::move(best_move);
        if (evasion_move) {
          pv += " " + USI::move(*evasion_move);
        }
        multi_pv_.Update(best_move, 0, pv);
      }
    }

    thpn = orig_thpn;
    thdn = orig_thdn;

    if (inc_flag > 0) {
      ExtendSearchThreshold(curr_result, thpn, thdn);
    }
  }

  return curr_result;
}

SearchResult KomoringHeights::SearchImpl(std::uint32_t thread_id,
                                         Node& n,
                                         PnDn thpn,
                                         PnDn thdn,
                                         MateLen len,
                                         std::uint32_t& inc_flag) {
  const PnDn orig_thpn = thpn;
  const PnDn orig_thdn = thdn;
  const std::uint32_t orig_inc_flag = inc_flag;

  auto& local_expansion = expansion_list_[thread_id].Current();
  monitor_.Visit(n.GetDepth());
  if (thread_id == 0 && monitor_.ShouldPrint()) {
    Print(n);
  }

  if (n.GetDepth() >= kDepthMax) {
    return SearchResult::MakeRepetition(n.OrHand(), len, 1, 0);
  }

  expansion_list_[thread_id].EliminateDoubleCount(tt_, n);

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

  if (thread_id == 0 && monitor_.ShouldCheckHashfull()) {
    if (tt_.Hashfull() >= kExecuteGcHashfullThreshold) {
      tt_.CollectGarbage(kGcRemovalRatio);
    }
    monitor_.ResetNextHashfullCheck();
  }

  while (!monitor_.ShouldStop(thread_id) && (curr_result.Pn() < thpn && curr_result.Dn() < thdn)) {
    // local_expansion.BestMove() にしたがい子局面を展開する
    // （curr_result.Pn() > 0 && curr_result.Dn() > 0 なので、BestMove が必ず存在する）
    const auto best_move = local_expansion.BestMove();
    const bool is_first_search = local_expansion.FrontIsFirstVisit();
    const BitSet64 sum_mask = local_expansion.FrontSumMask();
    const auto [child_thpn, child_thdn] = local_expansion.FrontPnDnThresholds(thpn, thdn);

    n.DoMove(best_move);

    // 子局面を展開する。展開した expansion は UndoMove() の直前に忘れずに開放しなければならない。
    auto& child_expansion = expansion_list_[thread_id].Emplace(tt_, n, len - 1, is_first_search, sum_mask);

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
    child_result = SearchImpl(thread_id, n, child_thpn, child_thdn, len - 1, inc_flag);

  CHILD_SEARCH_END:
    expansion_list_[thread_id].Pop();
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

std::optional<Move> KomoringHeights::GetEvasion(Node& n) {
  for (const auto move : MovePicker{n}) {
    const auto query = tt_.BuildChildQuery(n, move);
    bool does_have_old_child = false;
    const auto result =
        query.LookUp(does_have_old_child, kDepthMaxMateLen, [&n, &move = move]() { return InitialPnDn(n, move.move); });
    if (result.Dn() == 0) {
      return {move};
    }
  }

  return std::nullopt;
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
      SearchEntry(0, n, len);

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

UsiInfo KomoringHeights::CurrentInfo() const {
  UsiInfo usi_output = monitor_.GetInfo();
  usi_output.Set(UsiInfoKey::kHashfull, tt_.Hashfull());
  usi_output.Set(UsiInfoKey::kScore, score_.ToString());

  return usi_output;
}

void KomoringHeights::Print(const Node& n, bool force_print) {
  if (option_.silent) {
    return;
  }

  auto usi_output = CurrentInfo();
  if (!expansion_list_[0].IsEmpty()) {
    // 探索中なら現在の探索情報で multi_pv_ を更新する
    const auto& root = expansion_list_[0].Root();
    if (n.GetDepth() > 0) {
      multi_pv_.Update(root.BestMove(), n.GetDepth(), ToString(n.MovesFromStart()));
    }
    usi_output.Set(UsiInfoKey::kCurrMove, USI::move(root.BestMove()));
  } else {
    usi_output.Set(UsiInfoKey::kCurrMove, USI::move(best_moves_[0]));
  }

  if (force_print || (!after_final_ || option_.show_pv_after_mate)) {
    if (!expansion_list_[0].IsEmpty()) {
      const auto& root = expansion_list_[0].Root();
      for (const auto& [move, result] : Take(root.GetAllResults(), option_.multi_pv)) {
        auto score = Score::Make(option_.score_method, result, n.IsRootOrNode());
        score.AddOneIfFinal();
        const auto& [depth, pv] = multi_pv_[move];

        usi_output.PushPVBack(depth, score.ToString(), pv);
      }
    }
  }

  sync_cout << usi_output << sync_endl;
}

void KomoringHeights::PrintAtRoot(const Node& n) {
  expansion_list_[0].Emplace(tt_, n, kDepthMaxMateLen, true, BitSet64::Full(), option_.multi_pv);
  Print(n, true);
  expansion_list_[0].Pop();
}
}  // namespace komori
