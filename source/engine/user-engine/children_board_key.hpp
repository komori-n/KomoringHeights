#ifndef KOMORI_CHILDREN_BOARD_KEY_HPP_
#define KOMORI_CHILDREN_BOARD_KEY_HPP_

#include <array>

#include "move_picker.hpp"
#include "node.hpp"
#include "typedefs.hpp"

namespace komori {
/**
 * @brief 子局面の盤面ハッシュ値をコンストラクト時に計算して提供する。
 */
class ChildrenBoardKey {
 public:
  /**
   * @brief 局面 `n` の子局面すべてに対し盤面ハッシュ値を計算する。
   * @param n   現局面
   * @param mp  `n` における合法手
   */
  ChildrenBoardKey(const Node& n, const MovePicker& mp) {
    for (std::size_t i = 0; i < mp.size(); ++i) {
      const auto move = mp[i];
      keys_[i] = n.BoardKeyAfter(move.move);
    }
  }

  /// `i_raw` 番目の子の盤面ハッシュ値を返す。
  Key operator[](std::uint32_t i_raw) const { return keys_[i_raw]; }

 private:
  std::array<Key, kMaxCheckMovesPerNode> keys_;
};
}  // namespace komori

#endif  // KOMORI_CHILDREN_BOARD_KEY_HPP_