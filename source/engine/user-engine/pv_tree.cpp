#include "pv_tree.hpp"

#include "move_picker.hpp"

namespace komori {
namespace {
bool IsUpperBound(Bound bound) {
  return (bound & BOUND_UPPER) != 0;
}

bool IsLowerBound(Bound bound) {
  return (bound & BOUND_LOWER) != 0;
}

bool IsExactBound(Bound bound) {
  return bound == BOUND_EXACT;
}
}  // namespace

void PvTree::Clear() {
  entries_.clear();
}

void PvTree::Insert(Node& n, Bound bound, MateLen mate_len, Move best_move) {
  auto board_key = n.Pos().state()->board_key();
  auto or_hand = n.OrHand();

  // メモリ消費量をケチるために、他のエントリで代用できる場合は格納しないようにする
  bool need_store = false;
  auto probed_range = Probe(n);
  if (IsUpperBound(bound)) {
    if (probed_range.max_mate_len != mate_len || probed_range.best_move != best_move) {
      need_store = true;
    }
  }

  if (IsLowerBound(bound)) {
    if (probed_range.min_mate_len != mate_len || probed_range.best_move != best_move) {
      need_store = true;
    }
  }

  if (need_store) {
    Entry entry{bound, mate_len, best_move};
    entries_.insert({board_key, {n.OrHand(), entry}});
  }
}

PvTree::MateRange PvTree::Probe(Node& n) const {
  auto board_key = n.Pos().state()->board_key();
  auto or_hand = n.OrHand();

  return ProbeImpl(board_key, or_hand, n.IsOrNode());
}

PvTree::MateRange PvTree::ProbeAfter(Node& n, Move move) const {
  auto board_key = n.Pos().board_key_after(move);
  auto or_hand = n.OrHandAfter(move);

  return ProbeImpl(board_key, or_hand, !n.IsOrNode());
}

std::vector<Move> PvTree::Pv(Node& n) const {
  std::vector<Move> pv;

  auto mate_range = Probe(n);
  auto best_move = mate_range.best_move;
  while (best_move != MOVE_NONE && !n.IsRepetitionAfter(best_move)) {
    pv.push_back(best_move);
    n.DoMove(best_move);
    mate_range = Probe(n);
    best_move = mate_range.best_move;
  }

  RollBack(n, pv);
  return pv;
}

void PvTree::Verbose(Node& n) const {
  std::vector<Move> pv;

  for (;;) {
    std::ostringstream oss;
    for (auto&& move : MovePicker{n}) {
      auto child_key = n.Pos().key_after(move.move);
      if (auto mate_range = ProbeAfter(n, move.move); mate_range.best_move != MOVE_NONE) {
        oss << " " << move.move << "(" << mate_range.min_mate_len << "/" << mate_range.max_mate_len << ")";
      } else {
        oss << " " << move.move << "(-1)";
      }
    }

    sync_cout << "info string [" << n.GetDepth() << "] " << oss.str() << sync_endl;
    if (auto best_move = Probe(n).best_move; best_move != MOVE_NONE && !n.IsRepetitionAfter(best_move)) {
      pv.push_back(best_move);
      n.DoMove(best_move);
    } else {
      break;
    }
  }

  RollBack(n, pv);
}

PvTree::MateRange PvTree::ProbeImpl(Key board_key, Hand or_hand, bool or_node) const {
  // <戻り値候補>
  MateRange range{kZeroMateLen, kMaxMateLen, MOVE_NONE};
  // </戻り値候補>

  // [begin, end) の中から最も良さそうなエントリを探す
  auto [begin, end] = entries_.equal_range(board_key);
  for (auto it = begin; it != end; ++it) {
    auto it_hand = it->second.first;
    auto [it_bound, it_mate_len, it_best_move] = it->second.second;

    if (or_hand == it_hand && IsExactBound(it_bound)) {
      return {it_mate_len, it_mate_len, it_best_move};
    }

    if (hand_is_equal_or_superior(or_hand, it_hand) && IsUpperBound(it_bound)) {
      range.max_mate_len = std::min(range.max_mate_len, it_mate_len);
      if (or_node || range.best_move == MOVE_NONE) {
        range.best_move = it_best_move;
      }
    }

    if (hand_is_equal_or_superior(it_hand, or_hand) && IsLowerBound(it_bound)) {
      range.min_mate_len = std::max(range.min_mate_len, it_mate_len);
      if (!or_node || range.best_move == MOVE_NONE) {
        range.best_move = it_best_move;
      }
    }
  }

  return range;
}
}  // namespace komori
