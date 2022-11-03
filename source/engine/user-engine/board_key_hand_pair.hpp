#ifndef KOMORI_BOARD_KEY_HAND_PAIR
#define KOMORI_BOARD_KEY_HAND_PAIR

#include <type_traits>

#include "typedefs.hpp"

namespace komori {
/**
 * @brief 盤面ハッシュ値と攻め方の持ち駒のペア。これがあれば TT から結果を取得できる。
 *
 * TT 内部でこの構造体を使うとメモリ消費量が増える可能性があるので注意。基本的に、TT の外側とやり取りする際だけ用いる。
 */
struct BoardKeyHandPair : DefineNotEqualByEqual<BoardKeyHandPair> {
  // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
  Key board_key;  ///< 盤面ハッシュ値
  Hand hand;      ///< 攻め方の持ち駒
  // NOLINTEND(misc-non-private-member-variables-in-classes)

  /// Default Constructor(default)
  BoardKeyHandPair() noexcept = default;
  /// Construct by `board_key` and `hand`
  constexpr BoardKeyHandPair(Key board_key, Hand hand) noexcept : board_key{board_key}, hand{hand} {}

  /// `lhs` と `rhs` が等しいかどうか
  friend constexpr bool operator==(const BoardKeyHandPair& lhs, const BoardKeyHandPair& rhs) noexcept {
    return lhs.board_key == rhs.board_key && lhs.hand == rhs.hand;
  }
};

static_assert(std::is_standard_layout_v<BoardKeyHandPair>, "BoardKeyHandPair must follow standard layout");
}  // namespace komori

#endif  // KOMORI_BOARD_KEY_HAND_PAIR
