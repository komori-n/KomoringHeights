#ifndef KOMORI_PATH_KEYS_HPP_
#define KOMORI_PATH_KEYS_HPP_

#include "typedefs.hpp"

namespace komori {
namespace detail {
/// 移動元に関する経路ハッシュ値
inline HASH_KEY g_move_from[SQ_NB_PLUS1][komori::kDepthMax];
/// 移動先に関する経路ハッシュ値
inline HASH_KEY g_move_to[SQ_NB_PLUS1][komori::kDepthMax];
/// 成りを区別するための経路ハッシュ値
inline HASH_KEY g_promote[komori::kDepthMax];
/// 駒打ちに関する経路ハッシュ値
inline HASH_KEY g_dropped_pr[PIECE_HAND_NB][komori::kDepthMax];
/// 駒強奪（無駄合防止探索用）に関する経路ハッシュ値
inline HASH_KEY g_stolen_pr[PIECE_HAND_NB][komori::kDepthMax];
}  // namespace detail

/**
 * @brief 経路ハッシュのテーブルを初期化する。
 *
 * 探索開始前に1回だけ呼び出す必要がある。
 */
inline void PathKeyInit() {
  using detail::g_dropped_pr;
  using detail::g_move_from;
  using detail::g_move_to;
  using detail::g_promote;
  using detail::g_stolen_pr;

  PRNG rng(334334);

  for (const auto sq : SQ) {
    for (std::size_t depth = 0; depth < kDepthMax; ++depth) {
      SET_HASH(g_move_from[sq][depth], rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>());
      SET_HASH(g_move_to[sq][depth], rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>());
    }
  }

  for (auto& promote_entry : g_promote) {
    SET_HASH(promote_entry, rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>());
  }

  for (PieceType pr = NO_PIECE_TYPE; pr < PIECE_HAND_NB; ++pr) {
    for (std::size_t depth = 0; depth < kDepthMax; ++depth) {
      SET_HASH(g_dropped_pr[pr][depth], rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>());
      SET_HASH(g_stolen_pr[pr][depth], rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>());
    }
  }
}

/**
 * @brief 現在の `path_key` と手 `move` から1手後の `path_key` を計算する。
 * @param[in] path_key 現在の経路ハッシュ値
 * @param[in] move     次の手
 * @param[in] depth    現在の探索深さ
 * @return Key `move` 後の経路ハッシュ値
 *
 * 深さ `depth` により経路ハッシュ値が異なる。`depth` の値に応じてハッシュ値を変えることで、手順が前後して同じ局面に
 * 至る経路の間でハッシュ値がかぶらないようにしている。
 */
constexpr inline Key PathKeyAfter(Key path_key, Move move, Depth depth) {
  const auto to = to_sq(move);
  path_key ^= detail::g_move_to[to][depth];
  if (is_drop(move)) {
    const auto pr = move_dropped_piece(move);
    path_key ^= detail::g_dropped_pr[pr][depth];
  } else {
    const auto from = from_sq(move);
    path_key ^= detail::g_move_from[from][depth];
    if (is_promote(move)) {
      path_key ^= detail::g_promote[depth];
    }
  }

  return path_key;
}

/**
 * @brief 1手後の `path_key` と手 `move` から現在の `path_key` を計算する。
 * @param[in] path_key `move` 後の経路ハッシュ値
 * @param[in] move     次の手
 * @param[in] depth    現在の探索深さ
 * @return `move` 前の経路ハッシュ値
 *
 * `depth` は `move` する直前の深さを渡す必要がある。すなわち、以下の2つの関数は互いに逆写像の関係になっている。
 *
 * - `PathKeyAfter(・, move, depth)`
 * - `PathKeyBefore(・, move, depth)`
 *
 * @note `PathKeyAfter` は XOR に基づく差分計算をしているため、逆写像も全く同じ関数で実現できる
 */
constexpr inline Key PathKeyBefore(Key path_key, Move move, Depth depth) {
  return PathKeyAfter(path_key, move, depth);
}

/**
 * @brief 相手の持ち駒 `stolen_pr` を1枚奪った後の `path_key` を計算する。
 * @param[in] path_key   現在の経路ハッシュ値
 * @param[in] stolen_pr  相手の持ち駒から奪う駒
 * @param[in] depth      現在の探索深さ
 * @return 持ち駒を奪った直後のハッシュ値
 */
constexpr inline Key PathKeyAfterSteal(Key path_key, PieceType stolen_pr, Depth depth) {
  return path_key ^ detail::g_stolen_pr[stolen_pr][depth];
}

/**
 * @brief 相手に持ち駒 `stolen_pr` を1枚プレゼントした後の `path_key` を計算する。
 * @param[in] path_key   現在の経路ハッシュ値
 * @param[in] given_pr   相手にプレゼントする駒
 * @param[in] depth      現在の探索深さ
 * @return 持ち駒を渡した直後のハッシュ値
 *
 * @note `PathKeyAfterGive` は XOR に基づく差分計算をしているため、逆写像も全く同じ関数で実現できる
 */
constexpr inline Key PathKeyAfterGive(Key path_key, PieceType given_pr, Depth depth) {
  return PathKeyAfterSteal(path_key, given_pr, depth);
}

}  // namespace komori

#endif  // KOMORI_PATH_KEYS_HPP_
