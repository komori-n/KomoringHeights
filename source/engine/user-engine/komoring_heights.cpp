#if !defined(USER_ENGINE)
#define USER_ENGINE
#endif

#include "komoring_heights.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "../../mate/mate.h"
#include "children_cache.hpp"
#include "hands.hpp"
#include "move_picker.hpp"
#include "node_history.hpp"
#include "path_keys.hpp"

namespace komori {
namespace {
constexpr std::size_t kDefaultHashSizeMb = 64;
constexpr std::int64_t kGcInterval = 3000;
constexpr PnDn kIncreaseDeltaThreshold = 1000;

/// TT の使用率が kGcHashfullThreshold を超えたら kGcHashfullRemoveRatio だけ削除する
constexpr int kGcHashfullThreshold = 850;
constexpr int kGcHashfullRemoveRatio = 300;

std::vector<std::pair<Move, SearchResult>> ExpandChildren(TranspositionTable& tt, const Node& n) {
  std::vector<std::pair<Move, SearchResult>> ret;
  for (auto&& move : MovePicker{n}) {
    auto query = tt.GetChildQuery(n, move.move);
    auto entry = query.LookUpWithoutCreation();
    SearchResult result{*entry, n.OrHandAfter(move.move)};
    ret.emplace_back(move.move, result);
  }

  return ret;
}

/**
 * @brief move から始まる置換表に保存された手順を返す
 */
std::optional<std::vector<Move>> ExpandBranch(TranspositionTable& tt, Node& n, Move move) {
  std::vector<Move> branch;
  Node n_copy = n.HistoryClearedNode();

  branch.emplace_back(move);
  n_copy.DoMove(move);
  for (;;) {
    bool or_node = n_copy.IsOrNode();
    Move move = tt.LookUpBestMove(n_copy);
    if (move != MOVE_NONE && (!n_copy.Pos().pseudo_legal(move) || !n_copy.Pos().legal(move))) {
      // 現局面の持ち駒 <= 証明駒  なので、置換表に保存された手を指せない可能性がある
      // このときは、子局面の中から一番よさげな手を適当に選ぶ必要がある
      Move best_move = MOVE_NONE;
      Depth mate_len = 0;
      for (const auto& m2 : MovePicker{n_copy}) {
        auto query = tt.GetChildQuery(n_copy, m2.move);
        auto entry = query.LookUpWithoutCreation();
        if (entry->GetNodeState() != NodeState::kProvenState) {
          continue;
        }

        auto child_mate_len = entry->GetSolutionLen(n_copy.OrHand());
        if ((or_node && child_mate_len + 1 < mate_len) || (!or_node && child_mate_len + 1 > mate_len)) {
          mate_len = child_mate_len;
          best_move = m2.move;
        }
      }
      move = best_move;
    }

    if (!n_copy.Pos().pseudo_legal(move) || !n_copy.Pos().legal(move) || n_copy.IsRepetitionAfter(move)) {
      break;
    }

    n_copy.DoMove(move);
    branch.emplace_back(move);
  }

  if (n_copy.IsOrNode() && !n_copy.Pos().in_check()) {
    // 高速1手詰めルーチンで解ける局面は置換表に登録されていないのでチェックする必要がある
    if (auto move = Mate::mate_1ply(n_copy.Pos()); move != MOVE_NONE) {
      n_copy.DoMove(move);
      branch.emplace_back(move);
    }
  }

  bool found_mate = true;
  if (n_copy.IsOrNode() || !MovePicker{n_copy}.empty()) {
    found_mate = false;
  }

  RollBack(n_copy, branch);

  if (found_mate) {
    return std::make_optional(std::move(branch));
  } else {
    return std::nullopt;
  }
}
}  // namespace

void SearchProgress::NewSearch() {
  start_time_ = std::chrono::system_clock::now();
  depth_ = 0;
  move_count_ = 0;
}

void SearchProgress::WriteTo(UsiInfo& output) const {
  auto curr_time = std::chrono::system_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - start_time_).count();
  time_ms = std::max(time_ms, decltype(time_ms){1});
  auto nps = move_count_ * 1000ULL / time_ms;

  output.Set(UsiInfo::KeyKind::kSelDepth, depth_)
      .Set(UsiInfo::KeyKind::kTime, time_ms)
      .Set(UsiInfo::KeyKind::kNodes, move_count_)
      .Set(UsiInfo::KeyKind::kNps, nps);
}

KomoringHeights::KomoringHeights() : tt_{kGcHashfullRemoveRatio} {}

void KomoringHeights::Init() {
  Resize(kDefaultHashSizeMb);
}

void KomoringHeights::Resize(std::uint64_t size_mb) {
  tt_.Resize(size_mb);
}

bool KomoringHeights::Search(Position& n, std::atomic_bool& stop_flag) {
  tt_.NewSearch();
  progress_.NewSearch();
  proof_tree_.Clear();
  stop_ = &stop_flag;
  gc_timer_.reset();
  last_gc_ = 0;
  best_moves_.clear();

  Node node{n};
  PnDn thpndn = 1;
  ChildrenCache cache{tt_, node, true, NodeTag<true>{}};
  SearchResult result = cache.CurrentResult(node);
  while (StripMaybeRepetition(result.GetNodeState()) == NodeState::kOtherState && !IsSearchStop()) {
    thpndn = std::max(2 * result.Pn(), 2 * result.Dn());
    thpndn = std::max(thpndn, result.Pn() + 1);
    thpndn = std::max(thpndn, result.Dn() + 1);
    thpndn = std::min(thpndn, kInfinitePnDn);
    score_ = Score::Unknown(result.Pn(), result.Dn());

    result = SearchImpl<true>(node, thpndn, thpndn, cache, false);
  }

  // <for-debug>
  auto query = tt_.GetQuery(node);
  auto amount = ToAmount(node.GetMoveCount());
  switch (result.GetNodeState()) {
    case NodeState::kProvenState:
      query.SetProven(result.ProperHand(), result.BestMove(), result.GetSolutionLen(), amount);
      break;
    case NodeState::kDisprovenState:
      query.SetDisproven(result.ProperHand(), result.BestMove(), result.GetSolutionLen(), amount);
      break;
    case NodeState::kRepetitionState:
      query.SetRepetition(amount);
      break;
    default:
      auto entry = query.LookUpWithCreation();
      entry->UpdatePnDn(result.Pn(), result.Dn(), amount);
  }

  std::ostringstream oss;
  auto entry = query.LookUpWithCreation();
  oss << *entry;
  auto usi_output = UsiInfo::String(oss.str());
  progress_.WriteTo(usi_output);
  sync_cout << usi_output << sync_endl;
  // </for-debug>

  if (result.GetNodeState() == NodeState::kProvenState) {
    auto best_move = node.Pos().to_move(tt_.LookUpBestMove(node));
    auto pv = ExpandBranch(tt_, node, best_move);
    if (pv) {
      proof_tree_.AddBranch(node, *pv);
      if (yozume_node_count_ > 0 && yozume_search_count_ > 0) {
        DigYozume(node);
      }
    }

    pv = proof_tree_.GetPv(node);
    if (pv) {
      best_moves_ = *pv;
    }

    score_ = Score::Proven(static_cast<Depth>(best_moves_.size()));
    if (best_moves_.size() % 2 != 1) {
      sync_cout << "info string Failed to detect PV" << sync_endl;
    }
    return true;
  } else {
    score_ = Score::Disproven();
    return false;
  }
}

void KomoringHeights::DigYozume(Node& n) {
  auto best_move = n.Pos().to_move(tt_.LookUpBestMove(n));
  std::vector<Move> best_moves;
  auto root_pv = ExpandBranch(tt_, n, best_move);
  if (root_pv) {
    best_moves = std::move(*root_pv);
  }
  RollForward(n, best_moves);

  std::uint64_t found_count = 0;
  Depth mate_len = kMaxNumMateMoves;
  while (!best_moves.empty()) {
    auto move = best_moves.back();
    best_moves.pop_back();

    n.UndoMove(move);
    proof_tree_.Update(n);
    if (IsSearchStop() || n.GetDepth() >= mate_len - 2 || found_count >= yozume_search_count_) {
      continue;
    }

    if (n.IsOrNode()) {
      // 最善手以外に詰み手順がないか探す
      for (auto&& m2 : MovePicker{n}) {
        if (proof_tree_.HasEdgeAfter(n, m2.move)) {
          // 既に木に追加されている
          continue;
        }

        auto query = tt_.GetChildQuery(n, m2.move);
        auto entry = query.LookUpWithoutCreation();
        if (entry->GetNodeState() == NodeState::kDisprovenState ||
            entry->GetNodeState() == NodeState::kRepetitionState || n.IsRepetitionOrInferiorAfter(m2.move)) {
          // 既に不詰が示されている
          continue;
        }

        if (StripMaybeRepetition(entry->GetNodeState()) == NodeState::kOtherState) {
          // 再探索
          auto amount = entry->GetSearchedAmount();
          auto move_count_org = n.GetMoveCount();

          n.DoMove(m2.move);
          auto max_search_node_org = max_search_node_;
          max_search_node_ = std::min(max_search_node_, n.GetMoveCount() + yozume_node_count_);
          ChildrenCache cache{tt_, n, false, NodeTag<false>{}};
          auto result = SearchImpl<false>(n, kInfinitePnDn, kInfinitePnDn, cache, false);
          max_search_node_ = max_search_node_org;
          n.UndoMove(m2.move);

          amount = Update(amount, n.GetMoveCount() - move_count_org);
          switch (result.GetNodeState()) {
            case NodeState::kProvenState:
              query.SetProven(result.ProperHand(), result.BestMove(), result.GetSolutionLen(), amount);
              break;
            case NodeState::kDisprovenState:
              query.SetDisproven(result.ProperHand(), result.BestMove(), result.GetSolutionLen(), amount);
              break;
            case NodeState::kRepetitionState:
              query.SetRepetition(amount);
              break;
            default:
              entry = query.LookUpWithoutCreation();
              entry->UpdatePnDn(result.Pn(), result.Dn(), amount);
          }

          entry = query.LookUpWithoutCreation();
        }

        if (entry->GetNodeState() == NodeState::kProvenState) {
          // 新しく手を見つけた
          ++found_count;

          auto new_branch = ExpandBranch(tt_, n, m2.move);
          if (new_branch) {
            proof_tree_.AddBranch(n, *new_branch);

            auto new_pv = proof_tree_.GetPv(n);
            if (new_pv) {
              RollForward(n, *new_pv);
              std::copy(new_pv->begin(), new_pv->end(), std::back_inserter(best_moves));
              mate_len = std::min(mate_len, static_cast<Depth>(best_moves.size()));
              break;
            }
          }
        }
      }
    } else {
      // AND node
      // 余詰探索の結果、AND node の最善手が変わっている可能性がある
      // 現在の詰み手順よりも長く生き延びられる手があるなら、そちらの読みを進めてみる
      for (auto&& m2 : MovePicker{n}) {
        if (!proof_tree_.HasEdgeAfter(n, m2.move)) {
          auto branch = ExpandBranch(tt_, n, m2.move);
          if (branch) {
            proof_tree_.AddBranch(n, *branch);
          }
        }
      }

      if (auto new_mate_len = proof_tree_.MateLen(n) + n.GetDepth(); new_mate_len > mate_len) {
        // こっちに逃げたほうが手数が伸びる
        auto best_branch = proof_tree_.GetPv(n);

        if (best_branch) {
          // 千日手が絡むと、pv.size() と MateLen() が一致しないことがある
          // これは、pv の中に best_moves で一度通った局面が含まれるときに発生する
          // このような AND node は深く探索する必要がない。なぜなら、best_move の選び方にそもそも問題があるためである
          mate_len = new_mate_len;
          RollForward(n, *best_branch);
          std::copy(best_branch->begin(), best_branch->end(), std::back_inserter(best_moves));
        }
      }
    }
  }
}

void KomoringHeights::ShowValues(Position& n, const std::vector<Move>& moves) {
  auto depth_max = static_cast<Depth>(moves.size());
  Key path_key = 0;
  Node node{n};
  for (Depth depth = 0; depth < depth_max; ++depth) {
    path_key = PathKeyAfter(path_key, moves[depth], depth);
    node.DoMove(moves[depth]);
  }

  for (auto&& move : MovePicker{node}) {
    auto query = tt_.GetChildQuery(node, move.move);
    auto entry = query.LookUpWithoutCreation();
    sync_cout << move.move << " " << *entry << sync_endl;
  }

  static_assert(std::is_signed_v<Depth>);
  for (Depth depth = depth_max - 1; depth >= 0; --depth) {
    node.UndoMove(moves[depth]);
  }
}

void KomoringHeights::ShowPv(Position& n) {
  Node node{n};
  std::vector<Move> moves;

  for (;;) {
    auto children = ExpandChildren(tt_, node);
    std::sort(children.begin(), children.end(), [&](const auto& lhs, const auto& rhs) {
      if (node.IsOrNode()) {
        if (lhs.second.Pn() != rhs.second.Pn()) {
          return lhs.second.Pn() < rhs.second.Pn();
        } else if (lhs.second.Pn() == 0 && rhs.second.Pn() == 0) {
          return lhs.second.GetSolutionLen() < rhs.second.GetSolutionLen();
        }

        if (lhs.second.Dn() != rhs.second.Dn()) {
          return lhs.second.Dn() > rhs.second.Dn();
        } else if (lhs.second.Dn() == 0 && rhs.second.Dn() == 0) {
          return lhs.second.GetSolutionLen() > rhs.second.GetSolutionLen();
        }
        return false;
      } else {
        if (lhs.second.Dn() != rhs.second.Dn()) {
          return lhs.second.Dn() < rhs.second.Dn();
        } else if (lhs.second.Dn() == 0 && rhs.second.Dn() == 0) {
          return lhs.second.GetSolutionLen() < rhs.second.GetSolutionLen();
        }

        if (lhs.second.Pn() != rhs.second.Pn()) {
          return lhs.second.Pn() > rhs.second.Pn();
        } else if (lhs.second.Pn() == 0 && rhs.second.Pn() == 0) {
          return lhs.second.GetSolutionLen() > rhs.second.GetSolutionLen();
        }
        return false;
      }
    });

    std::ostringstream oss;
    oss << "[" << node.GetDepth() << "] ";
    for (const auto& child : children) {
      if (child.second.Pn() == 0) {
        oss << child.first << "(+" << child.second.GetSolutionLen() << ") ";
      } else if (child.second.Dn() == 0) {
        oss << child.first << "(-" << child.second.GetSolutionLen() << ") ";
      } else {
        oss << child.first << "(" << ToString(child.second.Pn()) << "/" << ToString(child.second.Dn()) << ") ";
      }
    }
    sync_cout << oss.str() << sync_endl;

    if (children.empty() || (children[0].second.Pn() == 1 && children[0].second.Dn() == 1)) {
      break;
    }
    auto best_move = children[0].first;
    node.DoMove(best_move);
    moves.emplace_back(best_move);
    if (node.IsRepetition()) {
      break;
    }
  }

  // 高速 1 手詰めルーチンで解ける局面は置換表に登録されていない可能性がある
  if (node.IsOrNode()) {
    if (Move move = Mate::mate_1ply(node.Pos()); move != MOVE_NONE) {
      node.DoMove(move);
      moves.emplace_back(move);
    }
  }

  sync_cout << sync_endl;
  std::ostringstream oss;
  for (const auto& move : moves) {
    oss << move << " ";
  }
  sync_cout << "pv: " << oss.str() << sync_endl;

  sync_cout << node.Pos() << sync_endl;
  for (auto itr = moves.crbegin(); itr != moves.crend(); ++itr) {
    node.UndoMove(*itr);
  }
}

UsiInfo KomoringHeights::Info() const {
  UsiInfo usi_output{};
  progress_.WriteTo(usi_output);
  usi_output.Set(UsiInfo::KeyKind::kHashfull, tt_.Hashfull()).Set(UsiInfo::KeyKind::kScore, score_);

  return usi_output;
}

template <bool kOrNode>
SearchResult KomoringHeights::SearchImpl(Node& n, PnDn thpn, PnDn thdn, ChildrenCache& cache, bool inc_flag) {
  progress_.Visit(n.GetDepth(), n.GetMoveCount());

  if (print_flag_) {
    PrintProgress(n);
    print_flag_ = false;
  }

  // 深さ制限。これ以上探索を続けても詰みが見つかる見込みがないのでここで early return する。
  if (n.IsExceedLimit(max_depth_)) {
    return {NodeState::kRepetitionState, kMinimumSearchedAmount, kInfinitePnDn, 0, n.OrHand()};
  }

  auto curr_result = cache.CurrentResult(n);
  // 探索延長。浅い結果を参照している場合、無限ループになる可能性があるので少しだけ探索を延長する
  inc_flag = inc_flag || cache.DoesHaveOldChild();
  if (inc_flag && curr_result.Pn() > 0 && curr_result.Dn() > 0) {
    if constexpr (kOrNode) {
      thdn = Clamp(thdn, curr_result.Dn() + 1);
      if (kIncreaseDeltaThreshold < curr_result.Pn() && curr_result.Pn() < kInfinitePnDn) {
        thpn = Clamp(thpn, curr_result.Pn() + 1);
      }
    } else {
      thpn = Clamp(thpn, curr_result.Pn() + 1);
      if (kIncreaseDeltaThreshold < curr_result.Dn() && curr_result.Dn() < kInfinitePnDn) {
        thdn = Clamp(thdn, curr_result.Dn() + 1);
      }
    }
  }

  if (gc_timer_.elapsed() > last_gc_ + kGcInterval) {
    if (tt_.Hashfull() >= kGcHashfullThreshold) {
      tt_.CollectGarbage();
    }
    last_gc_ = gc_timer_.elapsed();
  }

  std::unordered_map<Move, ChildrenCache*> cache_cache;
  while (!IsSearchStop()) {
    if (curr_result.Pn() >= thpn || curr_result.Dn() >= thdn) {
      break;
    }

    // 最も良さげな子ノードを展開する
    auto best_move = cache.BestMove();
    bool is_first_search = cache.BestMoveIsFirstVisit();
    auto [child_thpn, child_thdn] = cache.ChildThreshold(thpn, thdn);
    if (is_first_search) {
      inc_flag = false;
    }

    auto move_count_org = n.GetMoveCount();
    n.DoMove(best_move);
    auto& child_cache = children_cache_.emplace(tt_, n, is_first_search, NodeTag<!kOrNode>{});
    SearchResult child_result;
    if (is_first_search) {
      child_result = child_cache.CurrentResult(n);
    } else {
      child_result = SearchImpl<!kOrNode>(n, child_thpn, child_thdn, child_cache, inc_flag);
    }

    children_cache_.pop();
    n.UndoMove(best_move);

    cache.UpdateFront(child_result, n.GetMoveCount() - move_count_org);
    curr_result = cache.CurrentResult(n);
  }

  return curr_result;
}

void KomoringHeights::PrintProgress(const Node& n) const {
  auto usi_output = Info();

  usi_output.Set(UsiInfo::KeyKind::kDepth, n.GetDepth());
#if defined(KEEP_LAST_MOVE)
  usi_output.Set(UsiInfo::KeyKind::kPv, n.Pos().moves_from_start());
#endif

  sync_cout << usi_output << sync_endl;
}

bool KomoringHeights::IsSearchStop() const {
  return progress_.MoveCount() > max_search_node_ || *stop_;
}

template SearchResult KomoringHeights::SearchImpl<true>(Node& n,
                                                        PnDn thpn,
                                                        PnDn thdn,
                                                        ChildrenCache& cache,
                                                        bool inc_flag);
template SearchResult KomoringHeights::SearchImpl<false>(Node& n,
                                                         PnDn thpn,
                                                         PnDn thdn,
                                                         ChildrenCache& cache,
                                                         bool inc_flag);
}  // namespace komori
