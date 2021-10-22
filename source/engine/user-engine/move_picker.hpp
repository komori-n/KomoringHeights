#ifndef MOVE_PICKER_HPP_
#define MOVE_PICKER_HPP_

namespace komori {

/// 詰将棋専用 MovePicker
template <bool kOrNode, bool kOrdering = false>
class MovePicker {
 public:
  MovePicker() = delete;
  MovePicker(const MovePicker&) = delete;
  MovePicker(MovePicker&&) = delete;
  MovePicker& operator=(const MovePicker&) = delete;
  MovePicker& operator=(MovePicker&&) = delete;
  ~MovePicker() = default;

  explicit MovePicker(const Position& n) {
    bool judge_check = false;
    if constexpr (kOrNode) {
      if (n.in_check()) {
        last_ = generateMoves<EVASIONS_ALL>(n, move_list_.data());
        // 逆王手になっているかチェックする必要がある
        judge_check = true;
      } else {
        last_ = generateMoves<CHECKS_ALL>(n, move_list_.data());
      }
    } else {
      last_ = generateMoves<EVASIONS_ALL>(n, move_list_.data());
    }

    // OrNodeで王手ではない手と違法手を取り除く
    last_ = std::remove_if(move_list_.data(), last_,
                           [&](const auto& m) { return (judge_check && !n.gives_check(m.move)) || !n.legal(m.move); });

    // オーダリング情報を付加したほうが定数倍速くなる
    if constexpr (kOrdering) {
      auto us = n.side_to_move();
      auto them = ~us;
      auto king_color = kOrNode ? them : us;
      auto king_sq = n.king_square(king_color);
      for (ExtMove* itr = move_list_.data(); itr != last_; ++itr) {
        const auto& move = itr->move;
        auto to = to_sq(move);
        auto attackers_to_us = n.attackers_to(us, to);
        auto attackers_to_them = n.attackers_to(them, to);
        auto pt = type_of(n.moved_piece_before(move));
        itr->value = 0;

        // 成れるのに成らない
        if (!is_drop(move) && !is_promote(move)) {
          auto from = from_sq(move);
          if ((pt == PAWN || pt == BISHOP || pt == ROOK)) {
            itr->value += 1000;  // 歩、角、飛車を成らないのは大きく減点する（打ち歩詰めの時以外は考える必要ない）
          }
        }

        itr->value -= 10 * dist(king_sq, to);

        if constexpr (kOrNode) {
          itr->value -= 2 * (attackers_to_us.pop_count() + is_drop(move)) - attackers_to_them.pop_count();
          itr->value -= pt;
        } else {
          if (pt == KING) {
            itr->value -= 15;
          }
          itr->value += attackers_to_them.pop_count() - 2 * attackers_to_us.pop_count();
        }
      }
    }
  }

  std::size_t size() const { return static_cast<std::size_t>(last_ - move_list_.data()); }
  ExtMove* begin() { return move_list_.data(); }
  ExtMove* end() { return last_; }
  bool empty() const { return size() == 0; }

 private:
  std::array<ExtMove, kMaxCheckMovesPerNode> move_list_;
  ExtMove* last_;
};

}  // namespace komori

#endif  // MOVE_PICKER_HPP_