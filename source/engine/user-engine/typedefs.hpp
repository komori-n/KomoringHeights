#ifndef TYPEDEFS_HPP_
#define TYPEDEFS_HPP_

#include <limits>

#include "../../extra/all.h"
#include "../../types.h"

namespace komori {
using PnDn = std::uint64_t;
/// pn/dn の最大値。オーバーフローを避けるために、max() より少し小さな値を設定する。
constexpr PnDn kInfinitePnDn = std::numeric_limits<PnDn>::max() / 2;

using Generation = std::uint32_t;

inline constexpr Generation kFirstSearch = 1;
inline constexpr Generation kMarkDeleted = 0;
inline constexpr Generation kProven = std::numeric_limits<Generation>::max();
inline constexpr Generation kNonRepetitionDisproven = kProven - 1;
inline constexpr Generation kRepetitionDisproven = kNonRepetitionDisproven - 1;

/// 何局面読んだら generation を進めるか
constexpr std::uint32_t kNumSearchedPerGeneration = 128;

inline constexpr Generation CalcGeneration(std::uint64_t num_searched) {
  return kMarkDeleted + static_cast<Generation>(num_searched / kNumSearchedPerGeneration);
}

/// move 後の手駒を返す
inline Hand AfterHand(const Position& n, Move move, Hand before_hand) {
  if (is_drop(move)) {
    auto pr = move_dropped_piece(move);
    if (hand_exists(before_hand, pr)) {
      sub_hand(before_hand, move_dropped_piece(move));
    }
  } else {
    if (auto to_pc = n.piece_on(to_sq(move)); to_pc != NO_PIECE) {
      auto pr = raw_type_of(to_pc);
      add_hand(before_hand, pr);
      // オーバーフローしてしまった場合はそっと戻しておく
      if (before_hand & HAND_BORROW_MASK) {
        sub_hand(before_hand, pr);
      }
    }
  }
  return before_hand;
}
}  // namespace komori

#endif  // TYPEDEFS_HPP_