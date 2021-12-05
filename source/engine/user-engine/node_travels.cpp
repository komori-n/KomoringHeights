#include "node_travels.hpp"

#include <variant>

#include "../../mate/mate.h"
#include "hands.hpp"
#include "move_picker.hpp"
#include "node.hpp"
#include "path_keys.hpp"
#include "transposition_table.hpp"
#include "ttcluster.hpp"

namespace {
using komori::kNullHand;
using komori::OrHand;

}  // namespace

namespace komori {
NodeTravels::NodeTravels(TranspositionTable& tt) : tt_{tt} {}

std::vector<Move> NodeTravels::MateMovesSearch(Node& n) {
  std::unordered_map<Key, MateMoveCache> mate_table;
  std::unordered_map<Key, Depth> search_history;
  MateMovesSearchImpl<true>(mate_table, search_history, n);

  std::vector<Move> moves;
  while (mate_table.find(n.Pos().key()) != mate_table.end()) {
    auto cache = mate_table[n.Pos().key()];
    if (cache.move == MOVE_NONE) {
      break;
    }

    moves.emplace_back(cache.move);
    n.DoMove(cache.move);
  }

  for (auto itr = moves.crbegin(); itr != moves.crend(); ++itr) {
    n.UndoMove(*itr);
  }

  return moves;
}

template <bool kOrNode>
std::pair<NodeTravels::NumMoves, Depth> NodeTravels::MateMovesSearchImpl(
    std::unordered_map<Key, MateMoveCache>& mate_table,
    std::unordered_map<Key, Depth>& search_history,
    Node& n) {
  auto key = n.Pos().key();
  if (auto itr = search_history.find(key); itr != search_history.end()) {
    // 探索中の局面にあたったら、不詰を返す
    return {{kNoMateLen, 0}, itr->second};
  }

  if (auto itr = mate_table.find(key); itr != mate_table.end()) {
    // 以前訪れたことがあるノードの場合、その結果をそのまま返す
    return {itr->second.num_moves, kNonRepetitionDepth};
  }

  if (kOrNode && !n.Pos().in_check()) {
    if (auto move = Mate::mate_1ply(n.Pos()); move != MOVE_NONE) {
      auto after_hand = AfterHand(n.Pos(), move, OrHand<kOrNode>(n.Pos()));
      NumMoves num_moves = {1, CountHand(after_hand)};
      mate_table[key] = {move, num_moves};
      return {num_moves, kNonRepetitionDepth};
    }
  }

  search_history.insert(std::make_pair(key, n.GetDepth()));
  auto& move_picker = pickers_.emplace(n.Pos(), NodeTag<kOrNode>{});
  auto picker_is_empty = move_picker.empty();

  MateMoveCache curr{};
  curr.num_moves.num = kOrNode ? kMaxNumMateMoves : 0;
  bool curr_capture = false;
  Depth rep_start = kNonRepetitionDepth;

  for (const auto& move : move_picker) {
    auto child_query = tt_.GetChildQuery<kOrNode>(n, move.move);
    auto child_entry = child_query.LookUpWithoutCreation();
    if (child_entry->GetNodeState() != NodeState::kProvenState) {
      if (!kOrNode) {
        // nomate
        curr = {};
        break;
      }
      continue;
    }

    auto child_capture = n.Pos().capture(move.move);
    n.DoMove(move.move);
    auto [child_num_moves, child_rep_start] = MateMovesSearchImpl<!kOrNode>(mate_table, search_history, n);
    n.UndoMove(move.move);

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
    curr.num_moves.surplus = OrHand<kOrNode>(n.Pos());
  }

  if (rep_start >= n.GetDepth()) {
    mate_table[key] = curr;
    if (rep_start == n.GetDepth() && curr.num_moves.num >= 0) {
      n.DoMove(curr.move);
      std::unordered_map<Key, Depth> new_search_history;
      MateMovesSearchImpl<!kOrNode>(mate_table, new_search_history, n);
      n.UndoMove(curr.move);
    }
  }

  return {curr.num_moves, rep_start};
}

template std::pair<NodeTravels::NumMoves, Depth> NodeTravels::MateMovesSearchImpl<false>(
    std::unordered_map<Key, MateMoveCache>& mate_table,
    std::unordered_map<Key, Depth>& search_history,
    Node& n);
template std::pair<NodeTravels::NumMoves, Depth> NodeTravels::MateMovesSearchImpl<true>(
    std::unordered_map<Key, MateMoveCache>& mate_table,
    std::unordered_map<Key, Depth>& search_history,
    Node& n);
}  // namespace komori