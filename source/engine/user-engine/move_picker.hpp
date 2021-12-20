#ifndef MOVE_PICKER_HPP_
#define MOVE_PICKER_HPP_

#include <array>

#include "typedefs.hpp"

namespace komori {

/// 詰将棋専用 MovePicker
class MovePicker {
 public:
  MovePicker() = delete;
  MovePicker(const MovePicker&) = delete;
  MovePicker(MovePicker&& rhs) noexcept = delete;
  MovePicker& operator=(const MovePicker&) = delete;
  MovePicker& operator=(MovePicker&& rhs) noexcept = delete;
  ~MovePicker() = default;

  template <bool kOrNode>
  explicit MovePicker(const Position& n, NodeTag<kOrNode>, bool ordering = false) {
    bool judge_check = false;
    ExtMove* last = nullptr;
    if constexpr (kOrNode) {
      if (n.in_check()) {
        last = generateMoves<EVASIONS_ALL>(n, move_list_.data());
        // 逆王手になっているかチェックする必要がある
        judge_check = true;
      } else {
        last = generateMoves<CHECKS_ALL>(n, move_list_.data());
      }
    } else {
      last = generateMoves<EVASIONS_ALL>(n, move_list_.data());
    }

    // OrNodeで王手ではない手と違法手を取り除く
    last = std::remove_if(move_list_.data(), last,
                          [&](const auto& m) { return (judge_check && !n.gives_check(m.move)) || !n.legal(m.move); });
    size_ = last - &move_list_[0];

    // オーダリング情報を付加したほうが定数倍速くなる
    if (ordering) {
      auto us = n.side_to_move();
      auto them = ~us;
      auto king_color = kOrNode ? them : us;
      auto king_sq = n.king_square(king_color);
      constexpr int kPtValues[] = {
          0, 1, 2, 2, 3, 5, 5, 5, 8, 5, 5, 5, 5, 8, 8, 8,
      };
      for (ExtMove* itr = move_list_.data(); itr != last; ++itr) {
        const auto& move = itr->move;
        auto to = to_sq(move);
        // auto attackers_to_us = n.attackers_to(us, to);
        // auto attackers_to_them = n.attackers_to(them, to);
        auto pt = type_of(n.moved_piece_before(move));
        itr->value = 0;

        // 成れるのに成らない
        if (!is_drop(move) && !is_promote(move)) {
          auto from = from_sq(move);
          if ((pt == PAWN || pt == BISHOP || pt == ROOK)) {
            itr->value += 100;  // 歩、角、飛車を成らないのは大きく減点する（打ち歩詰めの時以外は考える必要ない）
          }
        }

        itr->value -= kPtValues[pt];
        // itr->value -= 2 * (attackers_to_us.pop_count() + is_drop(move)) - attackers_to_them.pop_count();
        itr->value += dist(king_sq, to);
      }
    }
  }

  std::size_t size() const { return size_; }
  ExtMove* begin() { return move_list_.data(); }
  ExtMove* end() { return begin() + size_; }
  bool empty() const { return size() == 0; }

 private:
  std::array<ExtMove, kMaxCheckMovesPerNode> move_list_;
  std::size_t size_;
};

}  // namespace komori

#endif  // MOVE_PICKER_HPP_