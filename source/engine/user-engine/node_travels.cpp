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

}  // namespace

namespace komori {
NodeTravels::NodeTravels(TranspositionTable& tt) : tt_{tt} {}

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