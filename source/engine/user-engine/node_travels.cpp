#include "node_travels.hpp"

#include <variant>

#include "../../mate/mate.h"
#include "hands.hpp"
#include "move_picker.hpp"
#include "path_keys.hpp"
#include "transposition_table.hpp"
#include "ttcluster.hpp"

namespace {
using komori::kNullHand;
using komori::OrHand;

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

namespace komori {
NodeTravels::NodeTravels(TranspositionTable& tt) : tt_{tt} {}

template <bool kOrNode>
CommonEntry* NodeTravels::LeafSearch(std::uint64_t num_searches,
                                     Position& n,
                                     Depth depth,
                                     Depth remain_depth,
                                     const LookUpQuery& query) {
  if (Hand hand = CheckMate1Ply<kOrNode>(n); hand != kNullHand) {
    return query.SetWin<kOrNode>(hand, num_searches);
  }

  // stack消費を抑えるために、vectorの中にMovePickerを構築する
  // 関数から抜ける前に、必ず pop_back() しなければならない
  auto& move_picker = pickers_.emplace(n, NodeTag<kOrNode>{});
  CommonEntry* ret_entry = nullptr;
  {
    if (move_picker.empty()) {
      Hand hand = PostProcessLoseHand<kOrNode>(n, kOrNode ? CollectHand(n) : HAND_ZERO);
      ret_entry = query.SetLose<kOrNode>(hand, num_searches);
      goto SEARCH_FOUND;
    }

    if (remain_depth <= 1 || depth > kMaxNumMateMoves) {
      goto SEARCH_NOT_FOUND;
    }

    bool unknown_flag = false;
    HandSet lose_hand = kOrNode ? HandSet::Full() : HandSet::Zero();
    for (const auto& move : move_picker) {
      auto child_query = tt_.GetChildQuery<kOrNode>(n, move.move, depth + 1, query.PathKey());
      auto child_entry = child_query.LookUpWithoutCreation();

      if (!query.DoesStored(child_entry) || child_entry->IsFirstVisit()) {
        // 近接王手以外は時間の無駄なので無視する
        if (kOrNode && !IsStepCheck(n, move)) {
          unknown_flag = true;
          continue;
        }

        // まだ FirstSearch していなさそうな node なら掘り進めてみる
        DoMove(n, move.move, depth);
        child_entry = LeafSearch<!kOrNode>(num_searches, n, depth + 1, remain_depth - 1, child_query);
        UndoMove(n, move.move);
      }

      if ((kOrNode && child_entry->GetNodeState() == NodeState::kProvenState) ||
          (!kOrNode && child_entry->GetNodeState() == NodeState::kDisprovenState)) {
        // win
        ret_entry = query.SetWin<kOrNode>(ProperChildHand<kOrNode>(n, move.move, child_entry), num_searches);
        goto SEARCH_FOUND;
      } else if ((!kOrNode && child_entry->GetNodeState() == NodeState::kProvenState) ||
                 (kOrNode && child_entry->GetNodeState() == NodeState::kDisprovenState)) {
        // lose
        lose_hand.Update<kOrNode>(ProperChildHand<kOrNode>(n, move.move, child_entry));
      } else {
        // unknown
        unknown_flag = true;
      }
    }

    if (unknown_flag) {
      goto SEARCH_NOT_FOUND;
    } else {
      Hand hand = PostProcessLoseHand<kOrNode>(n, lose_hand.Get());
      ret_entry = query.SetLose<kOrNode>(hand, num_searches);
    }
  }

  // passthrough
SEARCH_FOUND:
  pickers_.pop();
  return ret_entry == nullptr ? query.LookUpWithCreation() : ret_entry;

SEARCH_NOT_FOUND:
  pickers_.pop();
  static CommonEntry entry;
  entry = {query.HashHigh(), UnknownData{1, 1, query.GetHand(), depth}};
  return &entry;
}

std::vector<Move> NodeTravels::MateMovesSearch(Position& n, Depth depth, Key path_key) {
  std::unordered_map<Key, MateMoveCache> mate_table;
  std::unordered_map<Key, Depth> search_history;
  MateMovesSearchImpl<true>(mate_table, search_history, n, depth, path_key);

  std::vector<Move> moves;
  while (mate_table.find(n.key()) != mate_table.end()) {
    auto cache = mate_table[n.key()];
    if (cache.move == MOVE_NONE) {
      break;
    }

    moves.emplace_back(cache.move);
    n.do_move(cache.move, st_info_[depth++]);
  }

  for (auto itr = moves.crbegin(); itr != moves.crend(); ++itr) {
    n.undo_move(*itr);
  }

  return moves;
}

template <bool kOrNode>
std::pair<NodeTravels::NumMoves, Depth> NodeTravels::MateMovesSearchImpl(
    std::unordered_map<Key, MateMoveCache>& mate_table,
    std::unordered_map<Key, Depth>& search_history,
    Position& n,
    Depth depth,
    Key path_key) {
  auto key = n.key();
  if (auto itr = search_history.find(key); itr != search_history.end()) {
    // 探索中の局面にあたったら、不詰を返す
    return {{kNoMateLen, 0}, itr->second};
  }

  if (auto itr = mate_table.find(key); itr != mate_table.end()) {
    // 以前訪れたことがあるノードの場合、その結果をそのまま返す
    return {itr->second.num_moves, kNonRepetitionDepth};
  }

  if (kOrNode && !n.in_check()) {
    if (auto move = Mate::mate_1ply(n); move != MOVE_NONE) {
      auto after_hand = AfterHand(n, move, OrHand<kOrNode>(n));
      NumMoves num_moves = {1, CountHand(after_hand)};
      mate_table[key] = {move, num_moves};
      return {num_moves, kNonRepetitionDepth};
    }
  }

  search_history.insert(std::make_pair(key, depth));
  auto& move_picker = pickers_.emplace(n, NodeTag<kOrNode>{});
  auto picker_is_empty = move_picker.empty();

  MateMoveCache curr{};
  curr.num_moves.num = kOrNode ? kMaxNumMateMoves : 0;
  bool curr_capture = false;
  Depth rep_start = kNonRepetitionDepth;

  for (const auto& move : move_picker) {
    auto child_query = tt_.GetChildQuery<kOrNode>(n, move.move, depth + 1, path_key);
    auto child_entry = child_query.LookUpWithoutCreation();
    if (child_entry->GetNodeState() != NodeState::kProvenState) {
      if (!kOrNode) {
        // nomate
        curr = {};
        break;
      }
      continue;
    }

    auto path_key_after = PathKeyAfter(path_key, move.move, depth);
    auto child_capture = n.capture(move.move);
    DoMove(n, move.move, depth);
    auto [child_num_moves, child_rep_start] =
        MateMovesSearchImpl<!kOrNode>(mate_table, search_history, n, depth + 1, path_key_after);
    UndoMove(n, move.move);

    rep_start = std::min(rep_start, child_rep_start);
    if (child_num_moves.num >= 0) {
      bool update = false;
      if ((kOrNode && curr.num_moves.num > child_num_moves.num + 1) ||
          (!kOrNode && curr.num_moves.num < child_num_moves.num + 1)) {
        update = true;
      } else if (curr.num_moves.num == child_num_moves.num + 1) {
        if (curr.num_moves.surplus > child_num_moves.surplus ||
            (curr.num_moves.surplus == child_num_moves.surplus && !curr_capture && child_capture)) {
          update = true;
        }
      }

      if (update) {
        curr.move = move.move;
        curr.num_moves.num = child_num_moves.num + 1;
        curr.num_moves.surplus = child_num_moves.surplus;
        curr_capture = child_capture;
      }
    } else if (!kOrNode) {
      // nomate
      curr = {};
      break;
    }
  }
  search_history.erase(key);
  pickers_.pop();

  if (!kOrNode && picker_is_empty) {
    curr.num_moves.num = 0;
    curr.num_moves.surplus = OrHand<kOrNode>(n);
  }

  if (rep_start >= depth) {
    mate_table[key] = curr;
    if (rep_start == depth && curr.num_moves.num >= 0) {
      auto path_key_after = PathKeyAfter(path_key, curr.move, depth);
      DoMove(n, curr.move, depth);
      std::unordered_map<Key, Depth> new_search_history;
      MateMovesSearchImpl<!kOrNode>(mate_table, new_search_history, n, depth + 1, path_key_after);
      UndoMove(n, curr.move);
    }
  }

  return {curr.num_moves, rep_start};
}

template CommonEntry* NodeTravels::LeafSearch<false>(std::uint64_t num_searches,
                                                     Position& n,
                                                     Depth depth,
                                                     Depth max_depth,
                                                     const LookUpQuery& query);
template CommonEntry* NodeTravels::LeafSearch<true>(std::uint64_t num_searches,
                                                    Position& n,
                                                    Depth depth,
                                                    Depth max_depth,
                                                    const LookUpQuery& query);
template std::pair<NodeTravels::NumMoves, Depth> NodeTravels::MateMovesSearchImpl<false>(
    std::unordered_map<Key, MateMoveCache>& mate_table,
    std::unordered_map<Key, Depth>& search_history,
    Position& n,
    Depth depth,
    Key path_key);
template std::pair<NodeTravels::NumMoves, Depth> NodeTravels::MateMovesSearchImpl<true>(
    std::unordered_map<Key, MateMoveCache>& mate_table,
    std::unordered_map<Key, Depth>& search_history,
    Position& n,
    Depth depth,
    Key path_key);
}  // namespace komori