#include "proof_tree.hpp"

#include "move_picker.hpp"
#include "transposition_table.hpp"

namespace komori {
namespace {
constexpr int kMaxLoopUpdate = 10;
}

void ProofTree::AddBranch(Node& n, const std::vector<Move>& moves) {
  RollForward(n, moves);
  Update(n);
  RollBackAndUpdate(n, moves);
}

bool ProofTree::HasEdgeAfter(Node& n, Move16 move16) const {
  auto move = n.Pos().to_move(move16);
  auto key_after = n.Pos().key_after(move);

  return edges_.find(key_after) != edges_.end();
}

Depth ProofTree::MateLen(Node& n) const {
  auto key = n.Pos().key();
  if (auto itr = edges_.find(key); itr != edges_.end()) {
    return itr->second.mate_len;
  } else {
    return 0;
  }
}

std::optional<std::vector<Move>> ProofTree::GetPv(Node& n) {
  std::vector<Move> pv;
  bool found_pv = false;

  // ループによりスムーズに pv が求められないことがある
  // kMaxLoopUpdate 回まではループの解消を試みる
  for (int i = 0; !found_pv && i < kMaxLoopUpdate; ++i) {
    pv.clear();

    bool rep = false;
    for (;;) {
      // このタイミングで最善手を更新しないとループに迷い込むことがある
      Update(n);
      auto best_move = BestMove(n);
      if (best_move == MOVE_NONE || n.IsRepetitionAfter(best_move)) {
        // 局面がループしているので探索やり直し
        rep = true;
        break;
      }

      pv.emplace_back(best_move);
      n.DoMove(best_move);
    }

    if (!n.IsOrNode() && MovePicker{n}.empty()) {
      found_pv = true;
    }

    RollBack(n, pv);
    if (rep) {
      // 局面がループしている場合、ループの解消が必要。
      EliminateLoop(n);
    }
  }

  if (found_pv) {
    return std::make_optional(pv);
  } else {
    return std::nullopt;
  }
}

void ProofTree::Update(Node& n) {
  bool or_node = n.IsOrNode();

  Move best_move = MOVE_NONE;
  Depth mate_len = or_node ? kMaxNumMateMoves : 0;
  for (const auto& move : MovePicker{n}) {
    auto key_after = n.Pos().key_after(move.move);
    if (auto itr = edges_.find(key_after); itr != edges_.end()) {
      auto child_mate_len = itr->second.mate_len + 1;
      if ((or_node && mate_len > child_mate_len) || (!or_node && mate_len < child_mate_len)) {
        mate_len = child_mate_len;
        best_move = move.move;
      }
    }
  }

  auto key = n.Pos().key();
  edges_.insert_or_assign(key, Edge{best_move, mate_len});
}

void ProofTree::Verbose(Node& n) const {
  std::vector<Move> pv;

  for (;;) {
    std::ostringstream oss;
    for (auto&& move : MovePicker{n}) {
      auto child_key = n.Pos().key_after(move.move);
      if (auto itr = edges_.find(child_key); itr != edges_.end()) {
        oss << move.move << "(" << itr->second.mate_len << ") ";
      }
    }
    sync_cout << "info string [" << n.GetDepth() << "] " << oss.str() << sync_endl;

    auto best_move = BestMove(n);
    if (best_move == MOVE_NONE || n.IsRepetitionAfter(best_move)) {
      break;
    }

    pv.emplace_back(best_move);
    n.DoMove(best_move);
  }

  RollBack(n, pv);
}

void ProofTree::EliminateLoop(Node& n) {
  Node n_copy = n.HistoryClearedNode();
  std::unordered_set<Key> visited;

  EliminateLoopImpl(n_copy, visited);
}

Depth ProofTree::EliminateLoopImpl(Node& n, std::unordered_set<Key>& visited) {
  auto key = n.Pos().key();
  visited.insert(key);

  auto best_move = BestMove(n);
  if (best_move == MOVE_NONE) {
    return 0;
  }

  auto key_after = n.Pos().key_after(best_move);
  Depth mate_len = kMaxNumMateMoves;
  if (visited.find(key_after) == visited.end()) {
    n.DoMove(best_move);
    mate_len = EliminateLoopImpl(n, visited) + 1;
    n.UndoMove(best_move);
  }

  if (mate_len >= kMaxNumMateMoves && n.IsOrNode()) {
    mate_len = kMaxNumMateMoves;
    // best_move を選ぶとループしてしまうのでもっといい手があるはず
    auto& mp = pickers_.emplace(n);
    for (const auto& move : mp) {
      auto key_after = n.Pos().key_after(move.move);
      if (edges_.find(key_after) != edges_.end() && visited.find(key_after) == visited.end()) {
        n.DoMove(move);
        Depth child_mate_len = EliminateLoopImpl(n, visited) + 1;
        n.UndoMove(move);

        if ((n.IsOrNode() && child_mate_len < mate_len) || (!n.IsOrNode() && child_mate_len > mate_len)) {
          child_mate_len = mate_len;
          best_move = move.move;
        }
      }
    }
    pickers_.pop();
  }

  if (mate_len < kMaxNumMateMoves) {
    edges_.insert_or_assign(key, Edge{best_move, mate_len});
  }

  return mate_len;
}

Move ProofTree::BestMove(Node& n) const {
  auto itr = edges_.find(n.Pos().key());
  if (itr != edges_.end()) {
    return n.Pos().to_move(itr->second.best_move);
  } else {
    return MOVE_NONE;
  }
}

void ProofTree::RollForwardAndUpdate(Node& n, const std::vector<Move>& moves) {
  for (auto&& move : moves) {
    Update(n);
    n.DoMove(move);
  }
}

void ProofTree::RollBackAndUpdate(Node& n, const std::vector<Move>& moves) {
  for (auto itr = moves.crbegin(); itr != moves.crend(); ++itr) {
    n.UndoMove(*itr);
    Update(n);
  }
}

}  // namespace komori
