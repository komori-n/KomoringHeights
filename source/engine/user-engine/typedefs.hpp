#ifndef TYPEDEFS_HPP_
#define TYPEDEFS_HPP_

#include <limits>

#include "../../bitboard.h"
#include "../../position.h"
#include "../../types.h"

namespace komori {
/// 1局面の最大王手/王手回避の着手数
inline constexpr std::size_t kMaxCheckMovesPerNode = 100;
/// 詰将棋の最大手数。ミクロコスモス（1525手詰）より十分大きな値を設定する
constexpr Depth kMaxNumMateMoves = 3000;

/// 証明数／反証数を格納する型。将来、(1,1) 以外の初期値を使うことを考慮して 64 bit 分確保する。
using PnDn = std::uint64_t;
/// pn/dn の最大値。オーバーフローを避けるために、max() より少し小さな値を設定する。
constexpr PnDn kInfinitePnDn = std::numeric_limits<PnDn>::max() / 2;

/**
 * @brief 置換表世代（Generation）と局面状態（NodeState）を1つの整数にまとめたもの。
 *
 * [0, 30):  Generation
 * [31, 32): NodeState
 */
using StateGeneration = std::uint32_t;
/// 局面の状態（詰み、厳密な不詰、千日手による不詰、それ以外）を表す型
using NodeState = StateGeneration;
/// 置換表の世代を表す型。
using Generation = StateGeneration;

/// 削除候補（優先的にGCで消される）
inline constexpr StateGeneration kMarkDeleted = 0;
/// 未探索
inline constexpr StateGeneration kFirstSearch = 1;

/// StateGeneration から Generation 部分を得るためのマスク
inline constexpr StateGeneration kGenerationMask = 0x1fff'ffffu;
/// StateGeneration から NodeState 部分を得るためのマスク
inline constexpr StateGeneration kStateMask = 0xe000'0000u;
/// NodeState を得るためのビットシフト幅
inline constexpr int kStateShift = 29;

/// 千日手が絡まないノード
inline constexpr NodeState kOtherState = 0x00u;
/// 千日手による不詰かもしれない通常ノード
inline constexpr NodeState kMaybeRepetitionState = 0x01u;
/// 千日手ノード
inline constexpr NodeState kRepetitionState = 0x02u;
/// 不詰ノード
inline constexpr NodeState kDisprovenState = 0x03u;
/// 詰みノード
inline constexpr NodeState kProvenState = 0x04u;

/// 何局面読んだら generation を進めるか
constexpr std::uint32_t kNumSearchedPerGeneration = 128;
inline constexpr Generation CalcGeneration(std::uint64_t num_searched) {
  return kFirstSearch + static_cast<Generation>(num_searched / kNumSearchedPerGeneration);
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