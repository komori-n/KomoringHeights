/**
 * @file typedefs.hpp
 */
#ifndef KOMORI_TYPEDEFS_HPP_
#define KOMORI_TYPEDEFS_HPP_

#include <limits>
#include <string>

#include "../../bitboard.h"
#include "../../position.h"
#include "../../types.h"
#include "../../usi.h"
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

#define KOMORI_HAND_LOOP_UNROLL KOMORI_UNROLL(7)
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

/// 自分の thread id
thread_local inline std::uint32_t tl_thread_id = 0;
/// 自分は GC 担当スレッドかどうか
thread_local inline bool tl_gc_thread = false;

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
inline constexpr std::size_t kMaxCheckMovesPerNode = 110;
/// 詰将棋の最大手数。ミクロコスモス（1525手詰）より十分大きな値を設定する
inline constexpr Depth kDepthMax = 4000;
/// 無効な持ち駒
inline constexpr Hand kNullHand = Hand{HAND_BORROW_MASK};
/// 無効な Key
inline constexpr Key kNullKey = Key{0x3343343343343340ULL};

/**
 * @brief 局面の探索状態。
 */
enum class NodeState {
  kUnknown,     ///< 不明（探索中）
  kProven,      ///< 詰み
  kDisproven,   ///< 千日手ではない不詰
  kRepetition,  ///< 千日手による不詰
};

/**
 * @brief 証明数／反証数を格納する型
 *
 * 32ビット整数だとすぐにオーバーフローしてしまうので、64ビット整数を用いる。
 */
using PnDn = std::uint64_t;
/// pn/dn の最大値。オーバーフローを避けるために、max() より少し小さな値を設定する。
inline constexpr PnDn kInfinitePnDn = std::numeric_limits<PnDn>::max() / 2 - 1;
/// pn/dn 値の単位。df-pn+ では「評価値0.5」のような小数を扱いたいので1より大きな値を用いれるようにする。
inline constexpr PnDn kPnDnUnit = 2;
/**
 * @brief pn/dn の値を [`min`, `max`] の範囲に収まるように丸める。
 * @param[in] val pnまたはdn
 * @param[in] min 範囲の最小値
 * @param[in] max 範囲の最大値
 * @return PnDn [`min`, `max`] の範囲に丸めた `val`
 */
constexpr inline PnDn ClampPnDn(PnDn val, PnDn min = 0, PnDn max = kInfinitePnDn) {
  return std::clamp(val, min, max);
}

/**
 * @brief φ値を計算する。現局面が `or_node` なら `pn`, そうでないなら `dn` を返す。
 * @param[in] pn pn
 * @param[in] dn dn
 * @param[in] or_node 現局面が OR Node なら `true`
 * @return PnDn φ値
 */
constexpr inline PnDn Phi(PnDn pn, PnDn dn, bool or_node) noexcept {
  return or_node ? pn : dn;
}

/**
 * @brief δ値を計算する。現局面が `or_node` なら `dn`, そうでないなら `pn` を返す。
 * @param[in] pn pn
 * @param[in] dn dn
 * @param[in] or_node 現局面が OR Node なら `true`
 * @return PnDn δ値
 */
constexpr inline PnDn Delta(PnDn pn, PnDn dn, bool or_node) noexcept {
  return or_node ? dn : pn;
}

/// 探索量。TTでエントリを消す際の判断に用いる。
using SearchAmount = std::uint32_t;

/**
 * @brief pn/dn 値を文字列に変換する
 * @param val pn/dn 値
 * @return `val` の文字列表現
 */
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
 * @brief `Move` の Range オブジェクトをスペース区切りの `std::string` へ変換する
 *
 * @param range `Move` のリスト
 * @return `range` をスペース区切りで文字列化したもの
 */
template <typename Range,
          Constraints<decltype(std::declval<Range>().begin()), decltype(std::declval<Range>().end())> = nullptr>
inline std::string ToString(Range&& range) {
  std::string ret;
  for (auto&& move : std::forward<Range>(range)) {
    ret += USI::move(move);
    ret += ' ';
  }

  if (!ret.empty()) {
    ret.pop_back();
  }

  return ret;
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
  const auto us = n.side_to_move();
  const auto them = ~us;
  const auto hand = n.hand_of(us);
  const auto king_sq = n.king_square(them);
  const auto droppable_bb = ~n.pieces();
  KOMORI_HAND_LOOP_UNROLL for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
    if (hand_exists(hand, pr)) {
      if (pr == PAWN && (n.pieces(us, PAWN) & file_bb(file_of(king_sq)))) {
        continue;
      }

      if (n.check_squares(pr) & droppable_bb) {
        return true;
      }
    }
  }

  const auto x = ((n.pieces(PAWN) & check_candidate_bb(us, PAWN, king_sq)) |
                  (n.pieces(LANCE) & check_candidate_bb(us, LANCE, king_sq)) |
                  (n.pieces(KNIGHT) & check_candidate_bb(us, KNIGHT, king_sq)) |
                  (n.pieces(SILVER) & check_candidate_bb(us, SILVER, king_sq)) |
                  (n.pieces(GOLDS) & check_candidate_bb(us, GOLD, king_sq)) |
                  (n.pieces(BISHOP) & check_candidate_bb(us, BISHOP, king_sq)) | (n.pieces(ROOK_DRAGON)) |
                  (n.pieces(HORSE) & check_candidate_bb(us, ROOK, king_sq))) &
                 n.pieces(us);
  const auto y = n.blockers_for_king(them) & n.pieces(us);

  return x | y;
}
}  // namespace komori

#endif  // KOMORI_TYPEDEFS_HPP_
