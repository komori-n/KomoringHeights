#include "proof_tree.hpp"

#include "move_picker.hpp"
#include "transposition_table.hpp"

namespace komori {
void ProofTree::AddBranch(Node& n, const std::vector<Move>& moves) {
  RollForward(n, moves);

  // 末端局面から順に木に追加していく
  Update(n);
  for (auto itr = moves.crbegin(); itr != moves.crend(); ++itr) {
    n.UndoMove(*itr);
    Update(n);
  }
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

  for (;;) {
    // このタイミングで最善手を更新しないとループに迷い込むことがある
    Update(n);

    auto key = n.Pos().key();
    auto itr = edges_.find(key);
    if (itr == edges_.end()) {
      break;
    }

    auto best_move = itr->second.BestMove(n);
    if (best_move == MOVE_NONE || n.IsRepetitionAfter(best_move)) {
      break;
    }

    pv.emplace_back(best_move);
    n.DoMove(best_move);
  }

  bool found_pv = true;
  if (n.IsOrNode() || !MovePicker{n}.empty()) {
    found_pv = false;
  }

  for (auto itr = pv.crbegin(); itr != pv.crend(); ++itr) {
    n.UndoMove(*itr);
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

    auto key = n.Pos().key();
    auto itr = edges_.find(key);
    if (itr == edges_.end()) {
      break;
    }

    auto best_move = itr->second.BestMove(n);
    if (best_move == MOVE_NONE || n.IsRepetitionAfter(best_move)) {
      break;
    }

    pv.emplace_back(best_move);
    n.DoMove(best_move);
  }

  RollBack(n, pv);
}

}  // namespace komori
