#ifndef KOMORI_DELAYED_LIST_HPP_
#define KOMORI_DELAYED_LIST_HPP_

#include <array>
#include <optional>
#include <utility>

#include "move_picker.hpp"
#include "node.hpp"
#include "typedefs.hpp"

namespace komori {
/**
 * @brief 指し手の遅延展開を判断するクラス。
 *
 * 同じ地点への合駒などのすぐに展開する必要のない局面を特定し、その依存関係を提供する。
 *
 * もし合駒手を等しく均等に調べると、pn が過大評価される可能性があり、探索性能の劣化につながる。
 * そのため、他の指し手の結果を見てから局面を読み進めたいことがしばしばある。
 *
 * このクラスでは、遅延展開すべき手で双方向リストを構成する。例えば、`SQ_52` への合駒であれば、
 *
 * - △５二歩 -> △５二香 -> △５二桂 -> ... -> △５二金
 *
 * のような双方向リストを形成する。1つの局面に対し、複数個の双方向リストが構成されることもある。
 * 双方向リストの次の要素／前の要素はそれぞれ `Next()`/`Prev()` で取得できる。
 * 上の例では、それぞれの手の `Prev()`/`Next()` は次のようになる。
 *
 * Index | Move | Prev | Next
 * ------| -------- | -----|-------
 *     0 | △５二歩  | null | 1
 *     1 | △５二香  |    0 | 2
 *   ... |     ...  |  ... | ...
 *     6 | △５二金  |    5 | null
 */
class DelayedMoveList {
 public:
  /**
   * @brief 局面 `n` の遅延展開すべき手を調べる
   * @param n   現局面
   * @param mp  `n` における合法手
   */
  DelayedMoveList(const Node& n, const MovePicker& mp) {
    constexpr std::size_t kMaxLen = 10;
    std::array<std::pair<Move, std::size_t>, kMaxLen> moves;
    std::size_t len{0};

    std::size_t next_i_raw = 0;
    for (const auto& move : mp) {
      const auto i_raw = next_i_raw++;
      prev_[i_raw] = next_[i_raw] = 0;

      if (!IsDelayable(n, move)) {
        continue;
      }

      bool found = false;
      for (std::size_t j = 0; j < len; ++j) {
        const auto [move_j, j_raw] = moves[j];
        if (IsSame(move_j, move)) {
          next_[j_raw] = i_raw + 1;
          prev_[i_raw] = j_raw + 1;
          moves[j] = {move, i_raw};
          found = true;
          break;
        }
      }

      if (!found && len < kMaxLen) {
        moves[len++] = {move, i_raw};
      }
    }
  }

  /**
   * @brief `i_raw` の直前に展開すべき手のインデックスを返す。
   * @param i_raw 手の `index`
   * @return 直前に展開すべき手があればその `index`。なければ `std::nullopt`。
   */
  std::optional<std::uint32_t> Prev(std::uint32_t i_raw) const {
    if (auto dep = prev_[i_raw]) {
      return {dep - 1};
    }
    return std::nullopt;
  }

  /**
   * @brief `i_raw` の直後に展開すべき手のインデックスを返す。
   * @param i_raw 手の `index`
   * @return 直後に展開すべき手があればその `index`。なければ `std::nullopt`。
   */
  std::optional<std::uint32_t> Next(std::uint32_t i_raw) const {
    if (auto dep = next_[i_raw]) {
      return {dep - 1};
    }
    return std::nullopt;
  }

 private:
  /**
   * @brief `move` が遅延展開すべき手かどうか調べる。
   * @param n     現局面
   * @param move  `n` の合法手
   * @return `move` が遅延展開すべきなら `true`
   */
  bool IsDelayable(const Node& n, Move move) const {
    const Color us = n.Us();
    const auto to = to_sq(move);

    if (is_drop(move)) {
      if (n.IsOrNode()) {
        return false;
      } else {
        return true;
      }
    } else {
      const Square from = from_sq(move);
      const Piece moved_piece = n.Pos().piece_on(from);
      const PieceType moved_pr = type_of(moved_piece);
      if (enemy_field(us).test(from) || enemy_field(us).test(to)) {
        if (moved_pr == PAWN || moved_pr == BISHOP || moved_pr == ROOK) {
          return true;
        }

        if (moved_pr == LANCE) {
          if (us == BLACK) {
            return rank_of(to) == RANK_2;
          } else {
            return rank_of(to) == RANK_8;
          }
        }
      }
    }

    return false;
  }

  /**
   * @brief `m1` と `m2` が同様の手（片方を遅延展開すべき手）かどうか調べる。
   * @param m1 手1
   * @param m2 手2
   * @return `m1` と `m2` のどちらかを遅延展開すべきなら `true`
   * @note `m1` と `m2` はいずれも `IsDelayable() == true` でなければならない。
   */
  bool IsSame(Move m1, Move m2) const {
    const auto to1 = to_sq(m1);
    const auto to2 = to_sq(m2);
    if (is_drop(m1) && is_drop(m2)) {
      return to1 == to2;
    } else if (!is_drop(m1) && !is_drop(m2)) {
      const auto from1 = from_sq(m1);
      const auto from2 = from_sq(m2);
      return from1 == from2 && to1 == to2;
    } else {
      return false;
    }
  }

  std::array<std::uint32_t, kMaxCheckMovesPerNode> prev_;  ///< 直前に展開すべき手 + 1。なければ0。
  std::array<std::uint32_t, kMaxCheckMovesPerNode> next_;  ///< 直後に展開すべき手 + 1。なければ0。
};
}  // namespace komori

#endif  // KOMORI_DELAYED_LIST_HPP_
