#include "path_keys.hpp"

namespace {
HASH_KEY g_move_from[SQ_NB_PLUS1][komori::kMaxNumMateMoves];
HASH_KEY g_move_to[SQ_NB_PLUS1][komori::kMaxNumMateMoves];
HASH_KEY g_promote[komori::kMaxNumMateMoves];
HASH_KEY g_dropped_pr[PIECE_HAND_NB][komori::kMaxNumMateMoves];
}  // namespace

namespace komori {
void PathKeyInit() {
  PRNG rng(334334);

  for (auto sq : SQ) {
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
    }
  }
}

Key PathKeyAfter(Key path_key, Move move, Depth depth) {
  auto to = to_sq(move);
  path_key ^= g_move_to[to][depth];
  if (is_drop(move)) {
    auto pr = move_dropped_piece(move);
    path_key ^= g_dropped_pr[pr][depth];
  } else {
    auto from = from_sq(move);
    path_key ^= g_move_from[from][depth];
    if (is_promote(move)) {
      path_key ^= g_promote[depth];
    }
  }

  return path_key;
}
}  // namespace komori
