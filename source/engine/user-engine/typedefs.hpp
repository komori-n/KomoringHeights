/**
 * @file typedefs.hpp
 */
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
#include "type_traits.hpp"

#if defined(KOMORI_DEBUG)
/**
 * @brief `cond` が `true` かどうかをチェックするデバッグ用マクロ
 */
#define KOMORI_PRECONDITION(cond)                                                                                    \
  do {                                                                                                               \
    if (!(cond)) {                                                                                                   \
      sync_cout << "info string ERROR! precondition " << #cond << " @L" << __LINE__ << ":" << __FILE__ << sync_endl; \
      std::this_thread::sleep_for(std::chrono::seconds(1));                                                          \
      std::terminate();                                                                                              \
    }                                                                                                                \
  } while (false)
#else
/// 本番ビルド用
#define KOMORI_PRECONDITION(cond) ConsumeValues({cond})
#endif

#if !defined(DOXYGEN_SHOULD_SKIP_THIS)
// X を文字列にする。このままでは文字列化できないので IMPL マクロを経由する。
#define KOMORI_TO_STRING(X) KOMORI_TO_STRING_IMPL(X)
// X を文字列化する。
#define KOMORI_TO_STRING_IMPL(X) #X

#if defined(__clang__)
/// clang用 unroll
#define KOMORI_UNROLL(n) _Pragma(KOMORI_TO_STRING(unroll n))
#elif defined(__GNUC__)
/// GCC用 unroll
#define KOMORI_UNROLL(n) _Pragma(KOMORI_TO_STRING(GCC unroll n))
#else
/// pragma unroll
#define KOMORI_UNROLL(n)
#endif
#endif  // !defined(DOXYGEN_SHOULD_SKIP_THIS)

/// Komoring Heights
namespace komori {
// <namespaceコメント> NOLINTBEGIN
// Doxygen で名前空間を認識してもらうためにはコメントをつける必要がある。
// 各名前空間に対するコメントはどのヘッダに書いても良い。コメント位置を迷わないようにするためにすべてここに書いておく。
/// Detail
namespace detail {}  // namespace detail
/// TranspositionTable
namespace tt {
/// Detail
namespace detail {}  // namespace detail
}  // namespace tt
// </namespaceコメント> NOLINTEND

/**
 * @brief イテレータのペアで range-based for をするためのアダプタ
 * @tparam Iterator イテレータ
 *
 * `std::multimap::equal_range()` のように、(begin, end) の形式のイテレータを所持しているとき、range-based for で
 * 要素を取り出すためのアダプタ。
 *
 * ```cpp
 * std::unordered_multimap<std::int32_t, std::int32_t> map{ ... };
 * for (const auto& [key, value] : AsRange{map.equal_range(10)}) {
 *   ...
 * }
 * ```
 */
template <typename Iterator>
class AsRange {
 public:
  /// Default constructor(delte)
  AsRange() = delete;
  /**
   * @brief イテレータのペアから range を作成する
   * @param p イテレータのペア（begin, end）
   */
  constexpr explicit AsRange(const std::pair<Iterator, Iterator>& p) noexcept
      : begin_itr_{p.first}, end_itr_{p.second} {}
  /// 範囲の先頭
  constexpr Iterator begin() noexcept { return begin_itr_; }
  /// 範囲の末尾
  constexpr Iterator end() noexcept { return end_itr_; }

 private:
  Iterator begin_itr_;  ///< 範囲の先頭
  Iterator end_itr_;    ///< 範囲の末尾
};

/**
 * @brief `T` 型の値を足し合わせる。ただし、計算結果が `T` 型で表現できない場合は上限値で丸める（符号なし型）
 * @tparam T  足し合わせる型（符号なし型）
 * @param lhs `T` 型の値
 * @param rhs `T` 型の値
 * @return `lhs` と `rhs` を足し合わせた値
 */
template <typename T, Constraints<std::enable_if_t<std::is_unsigned_v<T>>> = nullptr>
constexpr inline T SaturatedAdd(T lhs, T rhs) noexcept {
  constexpr T kMax = std::numeric_limits<T>::max();
  if (kMax - lhs < rhs) {
    return kMax;
  }
  return lhs + rhs;
}

/**
 * @brief `T` 型の値を足し合わせる。ただし、計算結果が `T` 型で表現できない場合は上限値 or 下限値で丸める（符号つき型）
 * @tparam T  足し合わせる型（符号つき型）
 * @param lhs `T` 型の値
 * @param rhs `T` 型の値
 * @return `lhs` と `rhs` を足し合わせた値
 */
template <typename T, Constraints<std::enable_if_t<std::is_signed_v<T>>> = nullptr>
constexpr inline T SaturatedAdd(T lhs, T rhs) noexcept {
  constexpr T kMax = std::numeric_limits<T>::max();
  constexpr T kMin = std::numeric_limits<T>::min();

  if (lhs > 0 && rhs > 0) {
    if (kMax - lhs < rhs) {
      return kMax;
    }
  } else if (lhs < 0 && rhs < 0) {
    if (kMin - lhs > rhs) {
      return kMin;
    }
  }
  return lhs + rhs;
}

/**
 * @brief `T` 型の値を掛け合わせる。ただし、計算結果が `T` 型で表現できない場合は上限値で丸める（符号なし型）
 * @tparam T  掛け合わせる型（符号なし型）
 * @param lhs `T` 型の値
 * @param rhs `T` 型の値
 * @return `lhs` と `rhs` を掛け合わせた値
 */
template <typename T, Constraints<std::enable_if_t<std::is_unsigned_v<T>>> = nullptr>
constexpr inline T SaturatedMultiply(T lhs, T rhs) noexcept {
  constexpr T kMax = std::numeric_limits<T>::max();

  if (lhs == 0) {
    return 0;
  } else if (kMax / lhs < rhs) {
    return kMax;
  }

  return lhs * rhs;
}

/**
 * @brief `T` 型の値を掛け合わせる。ただし、計算結果が `T` 型で表現できない場合は上限値 or 下限値で丸める（符号つき型）
 * @tparam T  掛け合わせる型（符号つき型）
 * @param lhs `T` 型の値
 * @param rhs `T` 型の値
 * @return `lhs` と `rhs` を掛け合わせた値
 */
template <typename T, Constraints<std::enable_if_t<std::is_signed_v<T>>> = nullptr>
constexpr inline T SaturatedMultiply(T lhs, T rhs) noexcept {
  constexpr T kMax = std::numeric_limits<T>::max();
  constexpr T kMin = std::numeric_limits<T>::min();

  if (lhs == 0 || rhs == 0) {
    return 0;
  } else if (lhs > 0 && rhs > 0) {
    if (kMax / lhs < rhs) {
      return kMax;
    }
  } else if (lhs < 0 && rhs < 0) {
    if (kMax / lhs > rhs) {
      return kMax;
    }
  } else {
    // lhs と rhs は片方が正、もう片方が負

    if (rhs > 0) {
      // lhs に正の要素を持ってくる
      std::swap(lhs, rhs);
    }

    if (kMin / lhs > rhs) {
      return kMin;
    }
  }

  return lhs * rhs;
}

/// 1局面の最大王手/王手回避の着手数
inline constexpr std::size_t kMaxCheckMovesPerNode = 100;
/// 詰将棋の最大手数。ミクロコスモス（1525手詰）より十分大きな値を設定する
inline constexpr Depth kDepthMax = 4000;
/// 無効な持ち駒
inline constexpr Hand kNullHand = Hand{HAND_BORROW_MASK};
/// 無効な Key
inline constexpr Key kNullKey = Key{0x3343343343343340ULL};

/// OR Node/AND Node を表す型。コンパイル時分岐に用いる
template <bool kOrNode>
struct NodeTag {};

/**
 * @brief 証明数／反証数を格納する型
 *
 * 32ビット整数だとすぐにオーバーフローしてしまうので、64ビット整数を用いる。
 */
using PnDn = std::uint64_t;
/// pn/dn の最大値。オーバーフローを避けるために、max() より少し小さな値を設定する。
inline constexpr PnDn kInfinitePnDn = std::numeric_limits<PnDn>::max() / 2 - 1;
/// pnの初期値。df-pn+やdeep df-pnへの拡張を考慮して 1 ではない値で初期化できるようにしておく。
inline constexpr PnDn kInitialPn = 2;
/// dnの初期値。df-pn+やdeep df-pnへの拡張を考慮して 1 ではない値で初期化できるようにしておく。
inline constexpr PnDn kInitialDn = 2;
/**
 * @brief pn/dn の値を [`min`, `max`] の範囲に収まるように丸める。
 * @param[in] val pnまたはdn
 * @param[in] min 範囲の最小値
 * @param[in] max 範囲の最大値
 * @return PnDn [`min`, `max`] の範囲に丸めた `val`
 */
inline constexpr PnDn Clamp(PnDn val, PnDn min = 0, PnDn max = kInfinitePnDn) {
  return std::clamp(val, min, max);
}

/**
 * @brief φ値を計算する。現局面が `or_node` なら `pn`, そうでないなら `dn` を返す。
 * @param[in] pn pn
 * @param[in] dn dn
 * @param[in] or_node 現局面が OR Node なら `true`
 * @return PnDn φ値
 */
inline PnDn Phi(PnDn pn, PnDn dn, bool or_node) {
  return or_node ? pn : dn;
}

/**
 * @brief δ値を計算する。現局面が `or_node` なら `dn`, そうでないなら `pn` を返す。
 * @param[in] pn pn
 * @param[in] dn dn
 * @param[in] or_node 現局面が OR Node なら `true`
 * @return PnDn δ値
 */
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

/**
 * @brief 整数 `i` に対し、序数（Ordinal Number）の文字列を返す
 * @tparam Integer 整数型
 * @param i 値
 * @return `i` の序数表現（1st や 12th など）
 */
template <typename Integer>
inline std::string OrdinalNumber(Integer i) {
  static_assert(std::is_integral_v<Integer>);

  if ((i / 10) % 10 == 1) {
    return std::to_string(i) + "th";
  }

  switch (i % 10) {
    case 1:
      return std::to_string(i) + "st";
    case 2:
      return std::to_string(i) + "nd";
    case 3:
      return std::to_string(i) + "rd";
    default:
      return std::to_string(i) + "th";
  }
}

/**
 * @brief `c` 側の `sq` にある `pt` の短い利きの `Bitboard` を返す
 * @param pt 駒種
 * @param c  プレイヤー
 * @param sq 場所
 * @return Bitboard 短い利きの `Bitboard`
 */
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

/**
 * @brief (OR node限定) `n` が不詰かどうかを簡易的に調べる。
 * @param n 現局面（OR node）
 * @return `true`: 不明
 * @return `false`: 確実に不詰
 *
 * 指し手生成をすることなく `n` の合法手がない、すなわち不詰局面かどうかを判定する。`generateMoves` よりも
 * 厳密性は劣るが、より高速に不詰を判定できる可能性がある。
 *
 * この関数の戻り値が `false` のとき、`n` には合法手が存在しない。戻り値が `true` のとき、不詰かどうかは不明である。
 * 戻り値が `true` であっても、現局面に合法手が存在しない可能性があるので注意。
 */
inline bool DoesHaveMatePossibility(const Position& n) {
  auto us = n.side_to_move();
  auto them = ~us;
  auto hand = n.hand_of(us);
  auto king_sq = n.king_square(them);

  auto droppable_bb = ~n.pieces();
  for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
    if (hand_exists(hand, pr)) {
      if (pr == PAWN && (n.pieces(us, PAWN) & file_bb(file_of(king_sq)))) {
        continue;
      }

      if (droppable_bb.test(StepEffect(pr, them, king_sq))) {
        return true;
      }
    }
  }

  auto x = ((n.pieces(PAWN) & check_candidate_bb(us, PAWN, king_sq)) |
            (n.pieces(LANCE) & check_candidate_bb(us, LANCE, king_sq)) |
            (n.pieces(KNIGHT) & check_candidate_bb(us, KNIGHT, king_sq)) |
            (n.pieces(SILVER) & check_candidate_bb(us, SILVER, king_sq)) |
            (n.pieces(GOLDS) & check_candidate_bb(us, GOLD, king_sq)) |
            (n.pieces(BISHOP) & check_candidate_bb(us, BISHOP, king_sq)) | (n.pieces(ROOK_DRAGON)) |
            (n.pieces(HORSE) & check_candidate_bb(us, ROOK, king_sq))) &
           n.pieces(us);
  auto y = n.blockers_for_king(them) & n.pieces(us);

  return x | y;
}
}  // namespace komori

#endif  // KOMORI_TYPEDEFS_HPP_
