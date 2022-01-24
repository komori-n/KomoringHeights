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

/// 詰み／不詰手数とその手順における駒余り枚数を16bitでまとめた構造体。
struct MateLen {
  std::uint16_t len : 12;  ///< 詰み／不詰手数。kMaxNumMateLen が収まるギリギリのサイズを指定する
  std::uint16_t final_hand : 4;  ///< 詰み／不詰局面における駒余り枚数

  constexpr MateLen(std::uint16_t len, std::uint16_t final_hand) : len(len), final_hand(final_hand) {}
  MateLen() = default;
};

constexpr inline MateLen kZeroMateLen{std::uint16_t{0}, std::uint16_t{15}};
constexpr inline MateLen kMaxMateLen{std::uint16_t{kMaxNumMateMoves}, std::uint16_t{0}};

inline bool operator==(const MateLen& lhs, const MateLen& rhs) {
  return lhs.len == rhs.len && lhs.final_hand == rhs.final_hand;
}

inline bool operator!=(const MateLen& lhs, const MateLen& rhs) {
  return !(lhs == rhs);
}

/// MateLen 同士の比較をする。OR node から見て望ましい手順ほど小さく、AND node から見て望ましい手順ほど大きくする
inline bool operator<(const MateLen& lhs, const MateLen& rhs) {
  // 基本的には手数勝負
  if (lhs.len != rhs.len) {
    return lhs.len < rhs.len;
  }

  // 手数が同じ場合、駒余りの枚数で勝負
  // OR node:  たくさん余る手を選ぶ
  // AND node: できるだけ駒を使わせられる方に逃げる
  return lhs.final_hand > rhs.final_hand;
}

inline bool operator>(const MateLen& lhs, const MateLen& rhs) {
  return !(lhs < rhs) && !(lhs == rhs);
}

inline bool operator<=(const MateLen& lhs, const MateLen& rhs) {
  return lhs < rhs || lhs == rhs;
}

inline bool operator>=(const MateLen& lhs, const MateLen& rhs) {
  return lhs > rhs || lhs == rhs;
}

inline MateLen operator+(const MateLen& lhs, Depth d) {
  return MateLen{static_cast<std::uint16_t>(lhs.len + d), lhs.final_hand};
}

inline MateLen operator+(Depth d, const MateLen& rhs) {
  return MateLen{static_cast<std::uint16_t>(rhs.len + d), rhs.final_hand};
}

inline MateLen operator-(const MateLen& lhs, Depth d) {
  // len は unsigned なので、マイナスにならないようにする
  if (lhs.len < d) {
    return kZeroMateLen;
  }
  return MateLen{static_cast<std::uint16_t>(lhs.len - d), lhs.final_hand};
}

inline MateLen Min(const MateLen& lhs, const MateLen& rhs) {
  return lhs > rhs ? rhs : lhs;
}

inline MateLen Max(const MateLen& lhs, const MateLen& rhs) {
  return lhs < rhs ? rhs : lhs;
}

inline std::ostream& operator<<(std::ostream& os, const MateLen& mate_len) {
  os << mate_len.len << "(" << mate_len.final_hand << ")";
  return os;
}

}  // namespace komori

#endif  // TYPEDEFS_HPP_
