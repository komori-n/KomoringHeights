#ifndef MOVE_PICKER_HPP_
#define MOVE_PICKER_HPP_

#include <array>

#include "initial_estimation.hpp"
#include "node.hpp"
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

  explicit MovePicker(const Node& n, bool ordering = false) {
    bool judge_check = false;
    ExtMove* last = nullptr;
    bool or_node = n.IsOrNode();
    if (or_node) {
      if (n.Pos().in_check()) {
        last = generateMoves<EVASIONS_ALL>(n.Pos(), move_list_.data());
        // 逆王手になっているかチェックする必要がある
        judge_check = true;
      } else {
        last = generateMoves<CHECKS_ALL>(n.Pos(), move_list_.data());
      }
    } else {
      last = generateMoves<EVASIONS_ALL>(n.Pos(), move_list_.data());
    }

    // OrNodeで王手ではない手と違法手を取り除く
    last = std::remove_if(move_list_.data(), last, [&](const auto& m) {
      return (judge_check && !n.Pos().gives_check(m.move)) || !n.Pos().legal(m.move);
    });
    size_ = last - &move_list_[0];

    // オーダリング情報を付加したほうが定数倍速くなる
    if (ordering) {
      for (auto& move : *this) {
        move.value = MoveBriefEvaluation(n, move.move);
      }
    }
  }

  std::size_t size() const { return size_; }
  ExtMove* begin() { return move_list_.data(); }
  const ExtMove* begin() const { return move_list_.data(); }
  ExtMove* end() { return begin() + size_; }
  const ExtMove* end() const { return begin() + size_; }
  bool empty() const { return size() == 0; }

  auto& operator[](std::size_t i) { return move_list_[i]; }

  const auto& operator[](std::size_t i) const { return move_list_[i]; }

 private:
  std::array<ExtMove, kMaxCheckMovesPerNode> move_list_;
  std::size_t size_;
};

}  // namespace komori

#endif  // MOVE_PICKER_HPP_
