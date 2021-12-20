#ifndef TYPEDEFS_HPP_
#define TYPEDEFS_HPP_

#include <cinttypes>
#include <iomanip>
#include <limits>
#include <string>

#include "../../bitboard.h"
#include "../../position.h"
#include "../../types.h"

namespace komori {
/// 1局面の最大王手/王手回避の着手数
inline constexpr std::size_t kMaxCheckMovesPerNode = 100;
/// 詰将棋の最大手数。ミクロコスモス（1525手詰）より十分大きな値を設定する
inline constexpr Depth kMaxNumMateMoves = 3000;
/// 無効な持ち駒
inline constexpr Hand kNullHand = Hand{HAND_BORROW_MASK};
/// 無効な Key
inline constexpr Key kNullKey = Key{0x3343343343343340ULL};

template <bool kOrNode>
struct NodeTag {};

/// 証明数／反証数を格納する型
using PnDn = std::uint64_t;
/// pn/dn の最大値。オーバーフローを避けるために、max() より少し小さな値を設定する。
inline constexpr PnDn kInfinitePnDn = std::numeric_limits<PnDn>::max() / 2;
/// pn/dn 値を [0, kInfinitePnDn] の範囲に収まるように丸める。
inline constexpr PnDn Clamp(PnDn val, PnDn min = 0, PnDn max = kInfinitePnDn) {
  return std::clamp(val, min, max);
}

/// 局面の状態（詰み、厳密な不詰、千日手による不詰、それ以外）を表す型
enum class NodeState : std::uint32_t {
  kOtherState,
  kMaybeRepetitionState,
  kRepetitionState,
  kDisprovenState,
  kProvenState,
};

inline std::ostream& operator<<(std::ostream& os, NodeState node_state) {
  os << static_cast<std::uint32_t>(node_state);
  return os;
}

inline NodeState StripMaybeRepetition(NodeState node_state) {
  return node_state == NodeState::kMaybeRepetitionState ? NodeState::kOtherState : node_state;
}

/**
 * @brief 置換表世代（Generation）と局面状態（NodeState）を1つの整数にまとめたもの。
 */
struct StateGeneration {
  NodeState node_state : 3;
  std::uint32_t generation : 29;
};

inline constexpr bool operator==(const StateGeneration& lhs, const StateGeneration& rhs) {
  return lhs.node_state == rhs.node_state && lhs.generation == rhs.generation;
}

inline constexpr bool operator!=(const StateGeneration& lhs, const StateGeneration& rhs) {
  return !(lhs == rhs);
}

/// 置換表の世代を表す型。
using Generation = std::uint32_t;

inline constexpr StateGeneration kMarkDeleted = {NodeState::kOtherState, 0};
inline constexpr StateGeneration kFirstSearch = {NodeState::kOtherState, 1};

/// 何局面読んだら generation を進めるか
inline constexpr std::uint32_t kNumSearchedPerGeneration = 128;
inline constexpr Generation CalcGeneration(std::uint64_t num_searched) {
  return 1 + static_cast<Generation>(num_searched / kNumSearchedPerGeneration);
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

inline bool IsStepCheck(const Position& n, Move move) {
  auto us = n.side_to_move();
  auto them = ~us;
  auto king_sq = n.king_square(them);
  Piece pc = n.moved_piece_after(move);
  PieceType pt = type_of(pc);

  return StepEffect(pt, us, to_sq(move)).test(king_sq);
}

inline std::string HexString(std::uint64_t x) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(16) << x;
  return ss.str();
}

}  // namespace komori

#endif  // TYPEDEFS_HPP_