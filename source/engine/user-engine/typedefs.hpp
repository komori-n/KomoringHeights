#ifndef TYPEDEFS_HPP_
#define TYPEDEFS_HPP_

#include <limits>

#include "../../extra/all.h"
#include "../../types.h"

namespace komori {
using PnDn = std::uint64_t;
/// pn/dn の最大値。オーバーフローを避けるために、max() より少し小さな値を設定する。
constexpr PnDn kInfinitePnDn = std::numeric_limits<PnDn>::max() / 2;
/// 1局面の最大王手/王手回避の着手数
inline constexpr std::size_t kMaxCheckMovesPerNode = 100;

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

/// c 側の sq にある pt の利き先の Bitboard を返す
inline Bitboard StepEffect(PieceType pt, Color c, Square sq) {
  switch (pt) {
    case PAWN:
    case LANCE:
      return pawnEffect(c, sq);
    case KNIGHT:
      return knightEffect(c, sq);
    case SILVER:
      return silverEffect(c, sq);
    case GOLD:
    case PRO_PAWN:
    case PRO_LANCE:
    case PRO_KNIGHT:
    case PRO_SILVER:
      return goldEffect(c, sq);
    case KING:
    case HORSE:
    case DRAGON:
    case QUEEN:
      return kingEffect(sq);
    case BISHOP:
      return bishopStepEffect(sq);
    case ROOK:
      return rookStepEffect(sq);
    default:
      return {};
  }
}

}  // namespace komori

#endif  // TYPEDEFS_HPP_