#ifndef TYPEDEFS_HPP_
#define TYPEDEFS_HPP_

#include <limits>

#include "../../bitboard.h"
#include "../../position.h"
#include "../../types.h"

namespace komori {
using PnDn = std::uint64_t;
/// pn/dn の最大値。オーバーフローを避けるために、max() より少し小さな値を設定する。
constexpr PnDn kInfinitePnDn = std::numeric_limits<PnDn>::max() / 2;
/// 1局面の最大王手/王手回避の着手数
inline constexpr std::size_t kMaxCheckMovesPerNode = 100;
/// 詰将棋の最大手数。ミクロコスモス（1525手詰）より十分大きな値を設定する
constexpr Depth kMaxNumMateMoves = 3000;

using StateGeneration = std::uint32_t;
using NodeState = StateGeneration;
using Generation = StateGeneration;

inline constexpr StateGeneration kFirstSearch = 1;
inline constexpr StateGeneration kMarkDeleted = 0;
inline constexpr int kStateShift = 30;
inline constexpr StateGeneration kGenerationMask = 0x3fff'ffffu;
inline constexpr StateGeneration kStateMask = 0xc000'0000u;
inline constexpr NodeState kProvenState = 0x03u;
inline constexpr NodeState kNonRepetitionDisprovenState = 0x02u;
inline constexpr NodeState kRepetitionDisprovenState = 0x01u;
inline constexpr NodeState kOtherState = 0x00u;

/// 何局面読んだら generation を進めるか
constexpr std::uint32_t kNumSearchedPerGeneration = 128;

inline constexpr Generation CalcGeneration(std::uint64_t num_searched) {
  return kMarkDeleted + static_cast<Generation>(num_searched / kNumSearchedPerGeneration);
}

inline constexpr Generation GetGeneration(StateGeneration s_gen) {
  return s_gen & kGenerationMask;
}

inline constexpr NodeState GetState(StateGeneration s_gen) {
  return s_gen >> kStateShift;
}

inline constexpr StateGeneration MakeStateGeneration(NodeState state, Generation generation) {
  return generation | (state << kStateShift);
}

inline constexpr StateGeneration UpdateState(NodeState state, StateGeneration s_gen) {
  return GetGeneration(s_gen) | (state << kStateShift);
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