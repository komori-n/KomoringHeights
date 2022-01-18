#include "proof_tree.hpp"

#include "move_picker.hpp"
#include "transposition_table.hpp"

namespace komori {
namespace {
constexpr int kMaxLoopUpdate = 10;
}

void ProofTree::AddBranch(Node& n, const std::vector<Move>& moves) {
  Node n_copy = n.HistoryClearedNode();

  RollForward(n_copy, moves);
  Update(n_copy);
  RollBackAndUpdate(n_copy, moves);
}

bool ProofTree::HasEdgeAfter(Node& n, Move16 move16) const {
  auto move = n.Pos().to_move(move16);
  auto key_after = n.Pos().key_after(move);

  return edges_.find(key_after) != edges_.end();
}

MateLen ProofTree::GetMateLen(Node& n) const {
  auto key = n.Pos().key();
  auto it = edges_.find(key);

  if (it == edges_.end()) {
    return kZeroMateLen;
  }

  return it->second.mate_len;
}

std::optional<std::vector<Move>> ProofTree::GetPv(Node& n) {
  // ループによりスムーズに pv が求められないことがある
  // kMaxLoopUpdate 回まではループの解消を試みる
  //
  // n のとり方によってはループが解消できないことがある。例えば、以下の局面グラフを考える。
  //
  // (root)                               (n)
  //   O --> A --> O --> A --> O --> A --> O --> A
  //               ^           |                 \
  //               \-----------+-----------------/.
  //                           |
  //                           v
  //                           A --> O --> A （詰み）
  //
  // O: OR node,    A: AND node
  //
  // 局面グラフに 6 手のループか発生しており、開始局面がループ中である状況を考える。root~n の手順を無視して
  // n を始点として探索すると 7 手詰みである。しかし root~n の手順を無視しない場合、n はどうやっても詰まない。
  // このように、渡された局面 n の時点ですでにループの中に入ってしまっている場合、ループから脱出できないケースが
  // 存在する。

  std::vector<Move> pv;
  bool found_pv = false;
  int retry = 0;
  while (!found_pv && retry < kMaxLoopUpdate) {
    pv.clear();

    for (;;) {
      // このタイミングで最善手を更新しないとループに迷い込むことがある
      Update(n);
      auto best_move = BestMove(n);
      if (best_move == MOVE_NONE) {
        break;
      }
      if (n.IsRepetitionAfter(best_move)) {
        // 局面がループしているので探索やり直し
        ++retry;
        EliminateLoop(n);
        break;
      }

      pv.emplace_back(best_move);
      n.DoMove(best_move);
    }

    if (!n.IsOrNode() && MovePicker{n}.empty()) {
      found_pv = true;
    }
    RollBack(n, pv);
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
  MateLen mate_len = or_node ? kMaxMateLen : MateLen{0, static_cast<std::uint16_t>(CountHand(n.OrHand()))};
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

MateLen ProofTree::EliminateLoopImpl(Node& n, std::unordered_set<Key>& visited) {
  auto key = n.Pos().key();
  visited.insert(key);

  Move best_move = MOVE_NONE;
  MateLen mate_len = kMaxMateLen;
  for (auto&& m1 : MovePicker{n}) {
    if (n.IsRepetitionAfter(m1.move)) {
      continue;
    }

    if (!HasEdgeAfter(n, m1.move)) {
      continue;
    }

    n.DoMove(m1.move);
    auto k1 = n.Pos().key();
    auto m2 = BestMove(n);
    auto child_mate_len = kMaxMateLen;
    if (m2 == MOVE_NONE) {
      best_move = m1.move;
      child_mate_len = {std::uint16_t{0}, static_cast<std::uint16_t>(CountHand(n.OrHand()))};
    } else if (!n.IsRepetitionAfter(m2) && HasEdgeAfter(n, m2)) {
      n.DoMove(m2);
      auto k2 = n.Pos().key();
      if (visited.find(k2) == visited.end()) {
        child_mate_len = EliminateLoopImpl(n, visited) + 1;
      }
      n.UndoMove(m2);
    }

    if (child_mate_len < kMaxMateLen) {
      edges_.insert_or_assign(k1, Edge{m2, child_mate_len});
    }

    if (child_mate_len + 1 < mate_len) {
      mate_len = child_mate_len + 1;
      best_move = m1.move;
    }

    n.UndoMove(m1.move);
  }

  if (mate_len < kMaxMateLen) {
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
