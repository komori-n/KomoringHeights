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

#if defined(KOMORI_DEBUG)
#define KOMORI_PRECONDITION(cond)                                                                                    \
  do {                                                                                                               \
    if (!(cond)) {                                                                                                   \
      sync_cout << "info string ERROR! precondition " << #cond << " @L" << __LINE__ << ":" << __FILE__ << sync_endl; \
      std::this_thread::sleep_for(std::chrono::seconds(1));                                                          \
      std::terminate();                                                                                              \
    }                                                                                                                \
  } while (false)
#else
#define KOMORI_PRECONDITION(cond) (void)(cond)
#endif

/**
 * @namespace komori
 * @brief created by komori
 */
namespace komori {
/**
 * @brief `T` をそのまま返すメタ関数。
 * @tparam T 型
 *
 * 意図しない型推論を防ぎたいときに用いる。
 */
template <typename T>
struct Identity {
  using type = T;  ///< `T` をそのまま返す。
};

/**
 * @brief SFINAE の制約を書くのに便利な型。
 * @tparam Args `std::enable_if_t` の列。
 *
 * `Args...` の中で `std::enable_if_t` による SFINAE 制約式を書くことができる。制約式がすべて真の場合に限り、
 * `Args...` の推論に成功し `std::nullptr_t` となる。
 *
 * 例）型 `T` がデフォルト構築可能なときに限り `Func()` を定義したいとき。
 *
 * ```cpp
 * template <typename T,
 *    Constraints<std::enable_if_t<std::is_default_constructible_v<U>>> = nullptr>
 * void Func(T t) {
 *   ...
 * }
 * ```
 */
template <typename... Args>
using Constraints = decltype(nullptr);

/**
 * @brief `operator==` から `operator!=` を自動定義するクラス。
 * @tparam T `operator==` が定義されたクラス
 *
 * `operator==` の定義から `operator!=` を自動定義するクラス。以下のように使用する。
 *
 * ```cpp
 * struct Hoge : DefineNotEqualByEqual<Hoge> {
 *   constexpr friend operator==(const Hoge& lhs, const Hoge& rhs) {
 *     ...
 *   }
 * };
 * ```
 *
 * 定義したいクラス `Hoge` に対し、 `DefineNotEqualByEqual<Hoge>` を継承させることで `operator!=` を自動定義できる。
 */
template <typename T>
struct DefineNotEqualByEqual {
  /// `lhs` と `rhs` が一致しないかどうか判定する（`operator==`からの自動定義）
  constexpr friend bool operator!=(const T& lhs, const T& rhs) noexcept(noexcept(lhs == rhs)) { return !(lhs == rhs); }
};

/**
 * @brief `operator==` および `operator<` から各種演算子を自動定義するクラス。
 * @tparam T `operator==` および `operator<` が定義されたクラス
 *
 * `operator==` および `operator<` の定義から以下の演算子を自動定義する。
 *
 * - `operator<=`
 * - `operator>`
 * - `operator>=`
 *
 * 詳しくは `DefineNotEqualByEauyl` も参照のこと。
 */
template <typename T>
struct DefineComparisonOperatorsByEqualAndLess {
  /// `lhs <= rhs` を判定する（`operator==` および `operator<` からの自動定義）
  constexpr friend bool operator<=(const T& lhs, const T& rhs) noexcept(noexcept(lhs < rhs) && noexcept(lhs == rhs)) {
    return lhs < rhs || lhs == rhs;
  }

  /// `lhs > rhs` を判定する（`operator==` および `operator<` からの自動定義）
  constexpr friend bool operator>(const T& lhs, const T& rhs) noexcept(noexcept(rhs < lhs)) { return rhs < lhs; }
  /// `lhs >= rhs` を判定する（`operator==` および `operator<` からの自動定義）
  constexpr friend bool operator>=(const T& lhs, const T& rhs) noexcept(noexcept(lhs < rhs)) { return !(lhs < rhs); }
};

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
inline constexpr PnDn kInfinitePnDn = std::numeric_limits<PnDn>::max() / 2;
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
