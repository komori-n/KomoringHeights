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

void PvTree::Insert(Node& n, const Entry& entry) {
  auto board_key = n.Pos().state()->board_key();
  auto or_hand = n.OrHand();

  // メモリ消費量をケチるために、他のエントリで代用できる場合は格納しないようにする
  bool need_store = true;

  // exact bound は必ず格納する。lower bound や upper bound なら他のエントリで表現可能なことがある
  if (!IsExactBound(entry.bound)) {
    // いったん probe してみて、同じ情報が取れているなら格納不要と判断する
    if (auto probed_entry = Probe(n)) {
      // entry の内容がだいたい一致していれば格納不要
      if (probed_entry->mate_len == entry.mate_len && probed_entry->best_move == entry.best_move &&
          (entry.bound & probed_entry->bound) != 0) {
        need_store = false;
      }
    }
  }

  if (need_store) {
    entries_.insert({board_key, {or_hand, entry}});
  }
}

std::optional<PvTree::Entry> PvTree::Probe(Node& n) const {
  auto board_key = n.Pos().state()->board_key();
  auto or_hand = n.OrHand();

  return ProbeImpl(board_key, or_hand, n.IsOrNode());
}

std::optional<PvTree::Entry> PvTree::ProbeAfter(Node& n, Move move) const {
  auto board_key = n.Pos().board_key_after(move);
  auto or_hand = n.OrHandAfter(move);

  return ProbeImpl(board_key, or_hand, !n.IsOrNode());
}

std::vector<Move> PvTree::Pv(Node& n) const {
  std::vector<Move> pv;

  auto entry = Probe(n);
  while (entry) {
    auto best_move = entry->best_move;
    if (best_move == MOVE_NONE || n.IsRepetitionAfter(best_move)) {
      break;
    }

    pv.push_back(best_move);
    n.DoMove(best_move);
    entry = Probe(n);
  }

  bool success = true;
  if (n.IsOrNode() || !MovePicker{n}.empty()) {
    success = false;
  }

  RollBack(n, pv);

  if (success) {
    return pv;
  }
  return {};
}

void PvTree::PrintYozume(Node& n) const {
  std::vector<Move> pv = Pv(n);
  auto pv_len = static_cast<Depth>(pv.size());

  for (auto&& move : pv) {
    if (n.IsOrNode()) {
      for (auto&& m2 : MovePicker{n}) {
        if (m2.move == move) {
          continue;
        }

        auto entry = ProbeAfter(n, m2.move);
        if (entry && IsExactBound(entry->bound)) {
          sync_cout << "info string " << n.GetDepth() + 1 << " " << m2.move << " " << entry->mate_len << sync_endl;
        }
      }
    }
    n.DoMove(move);
  }
  RollBack(n, pv);
}

void PvTree::Verbose(Node& n) const {
  std::vector<Move> pv;

  for (;;) {
    std::ostringstream oss;
    for (auto&& move : MovePicker{n}) {
      auto child_key = n.Pos().key_after(move.move);
      if (auto entry = ProbeAfter(n, move.move)) {
        oss << " " << move.move << "(" << entry->mate_len;
        if (entry->bound == BOUND_LOWER) {
          oss << "L";
        } else if (entry->bound == BOUND_UPPER) {
          oss << "U";
        }
        oss << ")";
      } else {
        oss << " " << move.move << "(-1)";
      }
    }

    sync_cout << "info string [" << n.GetDepth() << "] " << oss.str() << sync_endl;
    if (auto entry = Probe(n); entry) {
      auto best_move = entry->best_move;
      if (best_move == MOVE_NONE || n.IsRepetitionAfter(best_move)) {
        break;
      }

      pv.push_back(best_move);
      n.DoMove(best_move);
    } else {
      break;
    }
  }

  RollBack(n, pv);
}

std::optional<PvTree::Entry> PvTree::ProbeImpl(Key board_key, Hand or_hand, bool or_node) const {
  // <戻り値候補>
  Bound bound = BOUND_NONE;
  auto hand_count = CountHand(or_hand);
  MateLen mate_len = or_node ? kMaxMateLen : MateLen{0, static_cast<std::uint16_t>(hand_count)};
  Move best_move = MOVE_NONE;
  // </戻り値候補>

  // [begin, end) の中から最も良さそうなエントリを探す
  auto [begin, end] = entries_.equal_range(board_key);
  for (auto it = begin; it != end; ++it) {
    auto it_hand = it->second.first;
    auto [it_bound, it_mate_len, it_best_move] = it->second.second;

    if (or_hand == it_hand && IsExactBound(it_bound)) {
      // 厳密な探索結果があればそのまま返す
      return it->second.second;
    } else if (or_node && IsUpperBound(it_bound) && hand_is_equal_or_superior(or_hand, it_hand)) {
      // it_hand で高々 it_mate_len 手詰なので、or_hand ならもっと早く詰むはず
      // -> 現局面の upper bound は it_mate_len
      if (mate_len > it_mate_len) {
        mate_len = it_mate_len;
        // hand で着手可能な手は現局面でも着手可能（持ち駒かより多いので）
        best_move = it_best_move;
        bound = BOUND_UPPER;
      }
    } else if (!or_node && IsLowerBound(it_bound) && hand_is_equal_or_superior(it_hand, or_hand)) {
      // it_hand で詰ますのに最低でも it_mate_len 手かかるので、or_hand ならもっとかかるはず
      // -> 現局面の lower bound は it_mate_len
      if (mate_len < it_mate_len) {
        mate_len = it_mate_len;
        // hand で着手可能な手は現局面でも着手可能（持ち駒かより多いので）
        best_move = it_best_move;
        bound = BOUND_LOWER;
      }
    }
  }

  if (bound != BOUND_NONE) {
    return Entry{bound, mate_len, best_move};
  }

  return std::nullopt;
}
}  // namespace komori
