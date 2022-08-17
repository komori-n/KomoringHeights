#ifndef KOMORI_PATH_KEYS_HPP_
#define KOMORI_PATH_KEYS_HPP_

#include "typedefs.hpp"

namespace komori {
namespace detail {
inline HASH_KEY g_move_from[SQ_NB_PLUS1][komori::kMaxNumMateMoves];
inline HASH_KEY g_move_to[SQ_NB_PLUS1][komori::kMaxNumMateMoves];
inline HASH_KEY g_promote[komori::kMaxNumMateMoves];
inline HASH_KEY g_dropped_pr[PIECE_HAND_NB][komori::kMaxNumMateMoves];
inline HASH_KEY g_stolen_pr[PIECE_HAND_NB][komori::kMaxNumMateMoves];
}  // namespace detail

/// ハッシュを初期化する
inline void PathKeyInit() {
  using detail::g_dropped_pr;
  using detail::g_move_from;
  using detail::g_move_to;
  using detail::g_promote;
  using detail::g_stolen_pr;

  PRNG rng(334334);

  for (const auto sq : SQ) {
    for (std::size_t depth = 0; depth < kMaxNumMateMoves; ++depth) {
      SET_HASH(g_move_from[sq][depth], rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>());
      SET_HASH(g_move_to[sq][depth], rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>());
    }
  }

  for (auto& promote_entry : g_promote) {
    SET_HASH(promote_entry, rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>());
  }

  for (PieceType pr = NO_PIECE_TYPE; pr < PIECE_HAND_NB; ++pr) {
    for (std::size_t depth = 0; depth < kMaxNumMateMoves; ++depth) {
      SET_HASH(g_dropped_pr[pr][depth], rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>());
      SET_HASH(g_stolen_pr[pr][depth], rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>(), rng.rand<Key>());
    }
  }
}

/// 現在の path_key と手 move から1手後の path_key を計算する。値は深さ depth 依存。
inline Key PathKeyAfter(Key path_key, Move move, Depth depth) {
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

/// 相手の持ち駒 stolen_pr を 1 枚奪った後の path_key を計算する
inline Key PathKeyAfterSteal(Key path_key, PieceType stolen_pr, Depth depth) {
  return path_key ^ detail::g_stolen_pr[stolen_pr][depth];
}

inline Key PathKeyBefore(Key path_key, Move move, Depth depth) {
  // xor でハッシュを計算しているので、1手進めるのと同じ関数で戻せる
  return PathKeyAfter(path_key, move, depth);
}

inline Key PathKeyAfterGive(Key path_key, PieceType stolen_pr, Depth depth) {
  return PathKeyAfterSteal(path_key, stolen_pr, depth);
}

}  // namespace komori

#endif  // KOMORI_PATH_KEYS_HPP_
