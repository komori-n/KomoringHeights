#ifndef KOMORI_TYPEDEFS_HPP_
#define KOMORI_TYPEDEFS_HPP_

#include <cassert>
#include <chrono>
#include <cinttypes>
#include <iomanip>
#include <limits>
#include <string>
#include <thread>

#include "../../bitboard.h"
#include "../../position.h"
#include "../../types.h"

namespace komori {
template <typename... Args>
using Constraints = std::nullptr_t;

/// 1局面の最大王手/王手回避の着手数
inline constexpr std::size_t kMaxCheckMovesPerNode = 100;
/// 詰将棋の最大手数。ミクロコスモス（1525手詰）より十分大きな値を設定する
inline constexpr Depth kMaxNumMateMoves = 4000;
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
/// pnの初期値。df-pn+やdeep df-pnへの拡張を考慮して 1 ではない値で初期化できるようにしておく。
inline constexpr PnDn kInitialPn = 2;
/// dnの初期値。df-pn+やdeep df-pnへの拡張を考慮して 1 ではない値で初期化できるようにしておく。
inline constexpr PnDn kInitialDn = 2;
/// pn/dn 値を [0, kInfinitePnDn] の範囲に収まるように丸める。
inline constexpr PnDn Clamp(PnDn val, PnDn min = 0, PnDn max = kInfinitePnDn) {
  return std::clamp(val, min, max);
}
/// Phi値（OR node なら pn、AND node なら dn）を計算する。
inline PnDn Phi(PnDn pn, PnDn dn, bool or_node) {
  return or_node ? pn : dn;
}

/// Delta値（OR node ならdn、AND node なら pn）を計算する。
inline PnDn Delta(PnDn pn, PnDn dn, bool or_node) {
  return or_node ? dn : pn;
}

/// pn/dn 値を文字列に変換する。
inline std::string ToString(PnDn val) {
  if (val == kInfinitePnDn) {
    return "inf";
  } else if (val > kInfinitePnDn) {
    return "invalid";
  } else {
    return std::to_string(val);
  }
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
  ss << "0x" << std::hex << std::setfill('0') << std::setw(16) << x;
  return ss.str();
}

inline std::string HexString(std::uint32_t x) {
  std::stringstream ss;
  ss << "0x" << std::hex << std::setfill('0') << std::setw(8) << x;
  return ss.str();
}

template <typename T>
struct Identity {
  using type = T;
};

template <typename T>
struct DefineNotEqualByEqual {
  constexpr friend bool operator!=(const T& lhs, const T& rhs) noexcept(noexcept(lhs == rhs)) { return !(lhs == rhs); }
};

template <typename T>
struct DefineComparisonOperatorsByEqualAndLess {
  constexpr friend bool operator<=(const T& lhs, const T& rhs) noexcept(noexcept(lhs < rhs) && noexcept(lhs == rhs)) {
    return lhs < rhs || lhs == rhs;
  }

  constexpr friend bool operator>(const T& lhs, const T& rhs) noexcept(noexcept(rhs < lhs)) { return rhs < lhs; }
  constexpr friend bool operator>=(const T& lhs, const T& rhs) noexcept(noexcept(lhs < rhs)) { return !(lhs < rhs); }
};

template <typename N>
inline std::string OrdinalNumeral(N n) {
  static_assert(std::is_integral_v<N>);

  switch (n) {
    case N{1}:
      return "1st";
    case N{2}:
      return "2nd";
    case N{3}:
      return "3rd";
    default:
      return std::to_string(n) + "th";
  }
}
}  // namespace komori

#endif  // KOMORI_TYPEDEFS_HPP_
