#if !defined(USER_ENGINE)
#define USER_ENGINE
#endif

#include "komoring_heights.hpp"

#include <algorithm>
#include <cmath>

#include "../../mate/mate.h"
#include "hands.hpp"
#include "move_picker.hpp"
#include "node_history.hpp"
#include "node_travels.hpp"
#include "path_keys.hpp"

namespace komori {
namespace {
constexpr std::size_t kDefaultHashSizeMb = 1024;

/// FirstSearch の初期深さ。数値実験してみた感じたと、1 ではあまり効果がなく、3 だと逆に遅くなるので
/// 2 ぐらいがちょうどよい
template <bool kOrNode>
constexpr Depth kFirstSearchDepth = kOrNode ? 1 : 2;

template <bool kOrNode>
inline Hand ProperChildHand(const Position& n, Move move, komori::CommonEntry* child_entry) {
  if constexpr (kOrNode) {
    Hand after_hand = child_entry->ProperHand(komori::AfterHand(n, move, OrHand<kOrNode>(n)));
    return komori::BeforeHand(n, move, after_hand);
  } else {
    return child_entry->ProperHand(OrHand<kOrNode>(n));
  }
}

template <bool kOrNode>
inline Hand CheckMate1Ply(Position& n) {
  if constexpr (!kOrNode) {
    return kNullHand;
  }

  if (!n.in_check()) {
    if (auto move = Mate::mate_1ply(n); move != MOVE_NONE) {
      komori::HandSet proof_hand = komori::HandSet::Zero();
      auto curr_hand = OrHand<true>(n);

      StateInfo st_info;
      n.do_move(move, st_info);
      proof_hand |= komori::BeforeHand(n, move, komori::AddIfHandGivesOtherEvasions(n, HAND_ZERO));
      n.undo_move(move);

      proof_hand &= curr_hand;
      return proof_hand.Get();
    }
  }
  return kNullHand;
}

}  // namespace

void SearchProgress::NewSearch() {
  start_time_ = std::chrono::system_clock::now();
  depth_ = 0;
  node_ = 0;
}

void SearchProgress::WriteTo(UsiInfo& output) const {
  auto curr_time = std::chrono::system_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - start_time_).count();
  time_ms = std::max(time_ms, decltype(time_ms){1});
  auto nps = node_ * 1000ULL / time_ms;

  output.Set(UsiInfo::KeyKind::kSelDepth, depth_)
      .Set(UsiInfo::KeyKind::kTime, time_ms)
      .Set(UsiInfo::KeyKind::kNodes, node_)
      .Set(UsiInfo::KeyKind::kNps, nps);
}

void KomoringHeights::Init() {
  Resize(kDefaultHashSizeMb);
  selector_cache_.Resize(max_depth_ + 1);
}

void KomoringHeights::Resize(std::uint64_t size_mb) {
  tt_.Resize(size_mb);
}

bool KomoringHeights::Search(Position& n, std::atomic_bool& stop_flag) {
  tt_.NewSearch();
  progress_.NewSearch();
  stop_ = &stop_flag;

  Node node{n};
  auto query = tt_.GetQuery<true>(node);
  auto* entry = query.LookUpWithCreation();
  PnDn thpndn = 1;
  do {
    thpndn = std::max(2 * entry->Pn(), 2 * entry->Dn());
    thpndn = std::min(thpndn, kInfinitePnDn);
    score_ = Score::Unknown(entry->Pn(), entry->Dn());

    SearchImpl<true>(node, thpndn, thpndn, query, entry, false);
    entry = query.RefreshWithoutCreation(entry);
  } while (entry->GetNodeState() == NodeState::kOtherState ||
           entry->GetNodeState() == NodeState::kMaybeRepetitionState);

  // <for-debug>
  std::ostringstream oss;
  oss << *entry;
  auto usi_output = UsiInfo::String(oss.str());
  progress_.WriteTo(usi_output);
  sync_cout << usi_output << sync_endl;
  // </for-debug>

  if (entry->Pn() == 0 && extra_search_count_ > 0) {
    for (int i = 0; i < extra_search_count_; ++i) {
      auto best_moves = BestMoves(n);
      score_ = Score::Proven(best_moves.size());
      sync_cout << "info string yozume_search_cnt=" << i << ", mate_len=" << best_moves.size() << sync_endl;
      if (!ExtraSearch(node, best_moves)) {
        break;
      }
    }

    entry = query.RefreshWithoutCreation(entry);
  }

  stop_ = nullptr;
  if (entry->GetNodeState() == NodeState::kProvenState) {
    score_ = Score::Proven(BestMoves(n).size());
    return true;
  } else {
    score_ = Score::Disproven();
    return false;
  }
}

std::vector<Move> KomoringHeights::BestMoves(Position& n) {
  Node node{n};
  return node_travels_.MateMovesSearch(node);
}

void KomoringHeights::ShowValues(Position& n, const std::vector<Move>& moves) {
  auto depth_max = static_cast<Depth>(moves.size());
  Key path_key = 0;
  Node node{n};
  for (Depth depth = 0; depth < depth_max; ++depth) {
    path_key = PathKeyAfter(path_key, moves[depth], depth);
    node.DoMove(moves[depth]);
  }

  if (depth_max % 2 == 0) {
    for (auto&& move : MovePicker{n, NodeTag<true>{}}) {
      auto query = tt_.GetChildQuery<true>(node, move.move);
      auto entry = query.LookUpWithoutCreation();
      sync_cout << move.move << " " << *entry << sync_endl;
    }
  } else {
    for (auto&& move : MovePicker{n, NodeTag<false>{}}) {
      auto query = tt_.GetChildQuery<false>(node, move.move);
      auto entry = query.LookUpWithoutCreation();
      sync_cout << move.move << " " << *entry << sync_endl;
    }
  }

  static_assert(std::is_signed_v<Depth>);
  for (Depth depth = depth_max - 1; depth >= 0; --depth) {
    node.UndoMove(moves[depth]);
  }
}

UsiInfo KomoringHeights::Info() const {
  UsiInfo usi_output{};
  progress_.WriteTo(usi_output);
  usi_output.Set(UsiInfo::KeyKind::kHashfull, tt_.Hashfull()).Set(UsiInfo::KeyKind::kScore, score_);

  return usi_output;
}

void KomoringHeights::PrintDebugInfo() const {
  auto stat = tt_.GetStat();
  std::ostringstream oss;

  oss << "hashfull=" << stat.hashfull << " proven=" << stat.proven_ratio << " disproven=" << stat.disproven_ratio
      << " repetition=" << stat.repetition_ratio << " maybe_repetition=" << stat.maybe_repetition_ratio
      << " other=" << stat.other_ratio;

  sync_cout << UsiInfo::String(oss.str()) << sync_endl;
}

template <bool kOrNode>
void KomoringHeights::SearchImpl(Node& n,
                                 PnDn thpn,
                                 PnDn thdn,
                                 const LookUpQuery& query,
                                 CommonEntry* entry,
                                 bool inc_flag) {
  progress_.Visit(n.GetDepth());

  // 探索深さ上限 or 千日手 のときは探索を打ち切る
  if (n.IsExceedLimit(max_depth_) || n.IsRepetition()) {
    query.SetRepetition(progress_.NodeCount());
    return;
  }

  if (print_flag_) {
    PrintProgress(n);
    print_flag_ = false;
  }

  // 初探索の時は n 手詰めルーチンを走らせる
  if (entry->IsFirstVisit()) {
    SearchLeaf<kOrNode>(n, kFirstSearchDepth<kOrNode>, query);
    entry = query.RefreshWithCreation(entry);
    if (entry->Pn() == 0 || entry->Dn() == 0) {
      return;
    }
    inc_flag = false;
  }

  // スタックの消費を抑えめために、ローカル変数で確保する代わりにメンバで動的確保した領域を探索に用いる
  MoveSelector<kOrNode>* selector = &selector_cache_.EmplaceBack<kOrNode>(n, tt_);

  if (inc_flag || selector->DoesHaveOldChild()) {
    std::tie(thpn, thdn) = selector->ControlThreshold(thpn, thdn);
  }

  // これ以降で return する場合、node_history の復帰と selector の返却を行う必要がある。
  // これらの処理は、SEARCH_IMPL_RETURN ラベル以降で行っている。

  while (!progress_.IsEnd(max_search_node_) && !*stop_) {
    if (selector->Pn() == 0) {
      query.SetProven(selector->ProofHand(), progress_.NodeCount());
      goto SEARCH_IMPL_RETURN;
    } else if (selector->Dn() == 0) {
      if (selector->IsRepetitionDisproven()) {
        // 千日手のため負け
        query.SetRepetition(progress_.NodeCount());
      } else {
        // 普通に詰まない
        query.SetDisproven(selector->DisproofHand(), progress_.NodeCount());
      }
      goto SEARCH_IMPL_RETURN;
    }

    entry->UpdatePnDn(selector->Pn(), selector->Dn(), progress_.NodeCount());
    if (entry->Pn() >= thpn || entry->Dn() >= thdn) {
      goto SEARCH_IMPL_RETURN;
    }

    auto [child_thpn, child_thdn] = selector->ChildThreshold(thpn, thdn);
    auto best_move = selector->FrontMove();

    n.DoMove(best_move);
    SearchImpl<!kOrNode>(n, child_thpn, child_thdn, selector->FrontLookUpQuery(), selector->FrontEntry(),
                         inc_flag || selector->DoesHaveOldChild());
    n.UndoMove(best_move);

    // GC の影響で entry の位置が変わっている場合があるのでループの最後で再取得する
    entry = query.RefreshWithCreation(entry);
    selector->Update();
  }

SEARCH_IMPL_RETURN:
  // node_history の復帰と selector の返却を行う必要がある
  selector_cache_.PopBack<kOrNode>();
}

template <bool kOrNode>
void KomoringHeights::SearchLeaf(Node& n, Depth remain_depth, const LookUpQuery& query) {
  if (n.IsRepetition()) {
    query.SetRepetition(progress_.NodeCount());
    return;
  }

  if (Hand hand = CheckMate1Ply<kOrNode>(n.Pos()); hand != kNullHand) {
    query.SetProven(hand, progress_.NodeCount());
    return;
  }

  auto& move_picker = pickers_.emplace(n.Pos(), NodeTag<kOrNode>{});
  {
    if (move_picker.empty()) {
      Hand hand = PostProcessLoseHand<kOrNode>(n.Pos(), kOrNode ? CollectHand(n.Pos()) : HAND_ZERO);
      query.SetLose<kOrNode>(hand, progress_.NodeCount());
      goto SEARCH_LEAF_RETURN;
    }

    if (remain_depth <= 1 || n.IsExceedLimit(max_depth_)) {
      goto SEARCH_LEAF_RETURN;
    }

    bool unknown_flag = false;
    HandSet lose_hand = kOrNode ? HandSet::Full() : HandSet::Zero();
    for (const auto& move : move_picker) {
      auto child_query = tt_.GetChildQuery<kOrNode>(n, move.move);
      auto child_entry = child_query.LookUpWithoutCreation();

      if (!query.DoesStored(child_entry) || child_entry->IsFirstVisit()) {
        // 近接王手以外は時間の無駄なので無視する
        if (kOrNode && !IsStepCheck(n.Pos(), move)) {
          unknown_flag = true;
          continue;
        }

        // まだ FirstSearch していなさそうな node なら掘り進めてみる
        n.DoMove(move.move);
        SearchLeaf<!kOrNode>(n, remain_depth - 1, child_query);
        child_entry = child_query.LookUpWithoutCreation();
        n.UndoMove(move.move);
      }

      if ((kOrNode && child_entry->GetNodeState() == NodeState::kProvenState) ||
          (!kOrNode && child_entry->GetNodeState() == NodeState::kDisprovenState)) {
        // win
        query.SetWin<kOrNode>(ProperChildHand<kOrNode>(n.Pos(), move.move, child_entry), progress_.NodeCount());
        goto SEARCH_LEAF_RETURN;
      } else if ((!kOrNode && child_entry->GetNodeState() == NodeState::kProvenState) ||
                 (kOrNode && child_entry->GetNodeState() == NodeState::kDisprovenState)) {
        // lose
        lose_hand.Update<kOrNode>(ProperChildHand<kOrNode>(n.Pos(), move.move, child_entry));
      } else {
        // unknown
        unknown_flag = true;
      }
    }
    if (unknown_flag) {
      goto SEARCH_LEAF_RETURN;
    } else {
      Hand hand = PostProcessLoseHand<kOrNode>(n.Pos(), lose_hand.Get());
      query.SetLose<kOrNode>(hand, progress_.NodeCount());
    }
  }

SEARCH_LEAF_RETURN:
  pickers_.pop();
}

bool KomoringHeights::ExtraSearch(Node& n, std::vector<Move> best_moves) {
  // 詰みがちゃんと見つかっていない場合は、探索を打ち切る
  auto mate_depth = static_cast<Depth>(best_moves.size());
  if (mate_depth % 2 != 1) {
    return false;
  }

  // n - 2 手詰めを見つけたい --> depth=n-1 で探索を打ち切れば良い
  max_depth_ = mate_depth - 1;
  Key path_key = 0;
  for (Depth depth = 0; depth < mate_depth; ++depth) {
    path_key = PathKeyAfter(path_key, best_moves[depth], depth);
    n.DoMove(best_moves[depth]);
  }

  PrintProgress(n);

  static_assert(std::is_signed_v<Depth>);
  bool found = false;
  for (Depth depth = mate_depth - 1; depth >= 0; --depth) {
    n.UndoMove(best_moves[depth]);
    path_key = PathKeyBefore(path_key, best_moves[depth], depth);

    if (!found && depth % 2 == 0) {
      // OrNode のとき、別の手を選んで max_depth 以内に詰むかどうか調べてみる
      for (auto&& move : MovePicker{n.Pos(), NodeTag<true>{}}) {
        auto query = tt_.GetChildQuery<true>(n, move.move);
        auto entry = query.LookUpWithoutCreation();
        if (entry->Pn() == 0 || entry->Dn() == 0) {
          continue;
        }

        entry = query.RefreshWithCreation(entry);

        auto new_n = n.NewInstance();
        new_n.DoMove(move.move);
        SearchImpl<false>(new_n, kInfinitePnDn, kInfinitePnDn, query, entry, false);
        new_n.UndoMove(move.move);

        entry = query.RefreshWithoutCreation(entry);
        if (entry->Pn() == 0) {
          // 別の詰みが見つかった
          found = true;
          break;
        }
      }
    }
  }
  return found;
}

void KomoringHeights::PrintProgress(const Node& n) const {
  auto usi_output = Info();

  usi_output.Set(UsiInfo::KeyKind::kDepth, n.GetDepth());
#if defined(KEEP_LAST_MOVE)
  usi_output.Set(UsiInfo::KeyKind::kPv, n.Pos().moves_from_start());
#endif

  sync_cout << usi_output << sync_endl;
}

template void KomoringHeights::SearchImpl<true>(Node& n,
                                                PnDn thpn,
                                                PnDn thdn,
                                                const LookUpQuery& query,
                                                CommonEntry* entry,
                                                bool inc_flag);
template void KomoringHeights::SearchImpl<false>(Node& n,
                                                 PnDn thpn,
                                                 PnDn thdn,
                                                 const LookUpQuery& query,
                                                 CommonEntry* entry,
                                                 bool inc_flag);
template void KomoringHeights::SearchLeaf<true>(Node& n, Depth remain_depth, const LookUpQuery& query);
template void KomoringHeights::SearchLeaf<false>(Node& n, Depth remain_depth, const LookUpQuery& query);
}  // namespace komori
