#include "node_travels.hpp"

#include <variant>

#include "../../mate/mate.h"
#include "move_picker.hpp"
#include "path_keys.hpp"
#include "proof_hand.hpp"
#include "transposition_table.hpp"
#include "ttcluster.hpp"

namespace {
constexpr int kNonProven = -1;
constexpr int kRepetitionNonProven = -2;
constexpr Hand kNullHand = Hand{HAND_BORROW_MASK};

template <bool kOrNode>
inline Hand OrHand(const Position& n) {
  if constexpr (kOrNode) {
    return n.hand_of(n.side_to_move());
  } else {
    return n.hand_of(~n.side_to_move());
  }
}

template <bool kAndOperator>
inline void UpdateHandSet(komori::HandSet& hand_set, Hand hand) {
  if constexpr (kAndOperator) {
    hand_set &= hand;
  } else {
    hand_set |= hand;
  }
}

template <bool kOrNode>
inline auto* DeclareWin(std::uint64_t num_searches, const komori::LookUpQuery& query, Hand hand) {
  if constexpr (kOrNode) {
    return query.SetProven(hand, num_searches);
  } else {
    return query.SetDisproven(hand, num_searches);
  }
}

template <bool kOrNode>
inline auto* StoreLose(std::uint64_t num_searches, const komori::LookUpQuery& query, const Position& n, Hand hand) {
  if constexpr (kOrNode) {
    Hand disproof_hand = komori::RemoveIfHandGivesOtherChecks(n, hand);
    return query.SetDisproven(disproof_hand, num_searches);
  } else {
    Hand proof_hand = komori::AddIfHandGivesOtherEvasions(n, hand);
    return query.SetProven(proof_hand, num_searches);
  }
}

template <bool kOrNode>
inline Hand ProperChildHand(const Position& n, Move move, komori::CommonEntry* child_entry) {
  if constexpr (kOrNode) {
    Hand after_hand = child_entry->ProperHand(komori::AfterHand(n, move, OrHand<kOrNode>(n)));
    return komori::BeforeHand(n, move, after_hand);
  } else {
    return child_entry->ProperHand(OrHand<kOrNode>(n));
  }
}

/// OrNode で move が近接王手となるか判定する
inline bool IsStepCheck(const Position& n, Move move) {
  auto us = n.side_to_move();
  auto them = ~us;
  auto king_sq = n.king_square(them);
  Piece pc = n.moved_piece_after(move);
  PieceType pt = type_of(pc);

  return komori::StepEffect(pt, us, to_sq(move)).test(king_sq);
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
    return DeclareWin<kOrNode>(num_searches, query, hand);
  }

  // stack消費を抑えるために、vectorの中にMovePickerを構築する
  // 関数から抜ける前に、必ず pop_back() しなければならない
  auto& move_picker = pickers_.emplace(n, NodeTag<kOrNode>{});
  CommonEntry* ret_entry = nullptr;
  {
    if (move_picker.empty()) {
      ret_entry = StoreLose<kOrNode>(num_searches, query, n, kOrNode ? CollectHand(n) : HAND_ZERO);
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
        ret_entry = DeclareWin<kOrNode>(num_searches, query, ProperChildHand<kOrNode>(n, move.move, child_entry));
        goto SEARCH_FOUND;
      } else if ((!kOrNode && child_entry->GetNodeState() == NodeState::kProvenState) ||
                 (kOrNode && child_entry->GetNodeState() == NodeState::kDisprovenState)) {
        // lose
        UpdateHandSet<kOrNode>(lose_hand, ProperChildHand<kOrNode>(n, move.move, child_entry));
      } else {
        // unknown
        unknown_flag = true;
      }
    }

    if (unknown_flag) {
      goto SEARCH_NOT_FOUND;
    } else {
      UpdateHandSet<!kOrNode>(lose_hand, OrHand<kOrNode>(n));
      ret_entry = StoreLose<kOrNode>(num_searches, query, n, lose_hand.Get());
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

template <bool kOrNode>
std::pair<int, int> NodeTravels::MateMovesSearch(std::unordered_map<Key, Move>& memo,
                                                 Position& n,
                                                 int depth,
                                                 Key path_key) {
  auto key = n.key();
  if (auto itr = memo.find(key); itr != memo.end() || depth >= kMaxNumMateMoves) {
    return {kRepetitionNonProven, 0};
  }

  if (kOrNode && !n.in_check()) {
    if (auto move = Mate::mate_1ply(n); move != MOVE_NONE) {
      memo[key] = move;
      return {1, CountHand(OrHand<kOrNode>(n))};
    }
  }

  memo[key] = Move::MOVE_NONE;
  Move curr_move = Move::MOVE_NONE;
  Depth curr_depth = kOrNode ? kMaxNumMateMoves : 0;
  int curr_hand_count = 0;
  bool curr_capture = false;

  auto& move_picker = pickers_.emplace(n, NodeTag<kOrNode>{});
  for (const auto& move : move_picker) {
    auto child_query = tt_.GetChildQuery<kOrNode>(n, move.move, depth + 1, path_key);
    auto child_entry = child_query.LookUpWithoutCreation();
    if (child_entry->GetNodeState() != NodeState::kProvenState) {
      continue;
    }

    auto path_key_after = PathKeyAfter(path_key, move.move, depth);
    auto child_capture = n.capture(move.move);
    DoMove(n, move.move, depth);
    auto [child_depth, child_hand_count] = MateMovesSearch<!kOrNode>(memo, n, depth + 1, path_key_after);
    UndoMove(n, move.move);

    if (child_depth >= 0) {
      bool update = false;
      if ((kOrNode && curr_depth > child_depth + 1) || (!kOrNode && curr_depth < child_depth + 1)) {
        update = true;
      } else if (curr_depth == child_depth + 1) {
        if (curr_hand_count > child_hand_count ||
            (curr_hand_count == child_hand_count && !curr_capture && child_capture)) {
          update = true;
        }
      }

      if (update) {
        curr_move = move.move;
        curr_depth = child_depth + 1;
        curr_hand_count = child_hand_count;
        curr_capture = child_capture;
      }
    }
  }
  pickers_.pop();

  if (kOrNode && curr_depth == kMaxNumMateMoves) {
    memo.erase(key);
    return {kRepetitionNonProven, 0};
  }

  memo[key] = curr_move;
  return {curr_depth, curr_hand_count};
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
template std::pair<int, int> NodeTravels::MateMovesSearch<false>(std::unordered_map<Key, Move>& memo,
                                                                 Position& n,
                                                                 int depth,
                                                                 Key path_key);
template std::pair<int, int> NodeTravels::MateMovesSearch<true>(std::unordered_map<Key, Move>& memo,
                                                                Position& n,
                                                                int depth,
                                                                Key path_key);
}  // namespace komori