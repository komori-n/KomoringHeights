/**
 * @file mate_len.hpp
 */
#ifndef KOMORI_MATE_LEN_HPP_
#define KOMORI_MATE_LEN_HPP_

#include <algorithm>
#include <ostream>

#include "typedefs.hpp"

namespace komori {
namespace detail {
/**
 * @brief `MateLen` と `MateLen16` の実装本体。中身はほぼ同じなので一箇所にまとめる。
 * @tparam T 16ビット以上の符号なし整数型
 *
 * @note 初期値として「0手よりも小さい手数」を表現したいので、実際の手数に 1 を加えた値を保持する。
 */
template <typename T>
class MateLenImpl : DefineNotEqualByEqual<MateLenImpl<T>>, DefineComparisonOperatorsByEqualAndLess<MateLenImpl<T>> {
  /**
   * @brief 任意の整数型を基底に持つ `MateLenImpl` をフレンド指定する。
   * @tparam S 整数型
   *
   * コンストラクト時に `len_plus_1_` に直接アクセスするために必要。`Len()` は -1 手を 0 手に切り上げてしまうので、
   * friend 指定なしだと実装がやや難しい。
   */
  template <typename S>
  friend class MateLenImpl;

  static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>, "T must be an integer");
  static_assert(sizeof(T) >= 2, "The size of T must be greater than or equal to 2");

 public:
  /**
   * @brief `len` 手詰み（不詰）で初期化する
   * @param len 詰み（不詰）手数
   */
  constexpr explicit MateLenImpl(T len) noexcept : len_plus_1_{static_cast<T>(len + 1)} {}
  /**
   * @brief 他の整数を基底に持つ `len` から初期化する
   * @tparam S  整数型
   * @param len 詰み（不詰）手数
   */
  template <typename S>
  constexpr explicit MateLenImpl(const MateLenImpl<S>& len) noexcept : len_plus_1_{static_cast<T>(len.len_plus_1_)} {}
  /// Default constructor(default)
  MateLenImpl() noexcept = default;
  /// Copy constructor(default)
  constexpr MateLenImpl(const MateLenImpl&) noexcept = default;
  /// Move constructor(default)
  constexpr MateLenImpl(MateLenImpl&&) noexcept = default;
  /// Copy assign operator(default)
  constexpr MateLenImpl& operator=(const MateLenImpl&) noexcept = default;
  /// Move assign operator(default)
  constexpr MateLenImpl& operator=(MateLenImpl&&) noexcept = default;
  /// Destructor(default)
  ~MateLenImpl() = default;

  /// 詰み手数を返す
  constexpr T Len() const noexcept { return std::max<T>(len_plus_1_, 1) - 1; }

  /// `lhs` と `rhs` が同じ手数かどうか
  friend constexpr bool operator==(const MateLenImpl& lhs, const MateLenImpl& rhs) noexcept {
    return lhs.len_plus_1_ == rhs.len_plus_1_;
  }

  /// `lhs` より `rhs` のほうが大きい手数かどうか
  friend constexpr bool operator<(const MateLenImpl& lhs, const MateLenImpl& rhs) noexcept {
    return lhs.len_plus_1_ < rhs.len_plus_1_;
  }

  /// `lhs` に `rhs` を加えた手数
  friend constexpr MateLenImpl operator+(const MateLenImpl& lhs, const T& rhs) noexcept {
    return MateLenImpl{DirectConstructTag{}, static_cast<T>(lhs.len_plus_1_ + rhs)};
  }

  /// `lhs` に `rhs` を加えた手数
  friend constexpr MateLenImpl operator+(const T& lhs, const MateLenImpl& rhs) noexcept { return rhs + lhs; }

  /// `lhs` から `rhs` を引いた手数
  friend constexpr MateLenImpl operator-(const MateLenImpl& lhs, const T& rhs) noexcept {
    return MateLenImpl{DirectConstructTag{}, static_cast<T>(lhs.len_plus_1_ - rhs)};
  }

  /// 出力ストリームへの出力
  friend std::ostream& operator<<(std::ostream& os, const MateLenImpl len) {
    if (len.len_plus_1_ > 0) {
      return os << len.len_plus_1_ - 1;
    } else {
      return os << -1;
    }
  }

 private:
  /// `len_plus_1` からコンストラクトすることを示すタグ
  struct DirectConstructTag {};
  /**
   * @brief `len_plus_1` から直接構築するためのコンストラクタ
   * @param len_plus_1 詰み／不詰手数 + 1
   */
  constexpr MateLenImpl(DirectConstructTag, T len_plus_1) : len_plus_1_{len_plus_1} {}

  /// 詰み／不詰手数 + 1。「0手より小さい手」を初期値として使いたいので 1 を加える。
  T len_plus_1_;
};
}  // namespace detail

/**
 * @brief 詰み／不詰手数
 *
 * `kZeroMateLen` 以上 `kDepthMaxMateLen` 手以下の詰み手数を管理する。内部的には -1 手詰め（`kMinus1MateLen`）を
 * 表現できるようになっている。
 *
 * 範囲外の初期値がほしい場合は、`kMinus1MateLen` や `kDepthMaxPlus1MateLen` を用いる。
 */
using MateLen = detail::MateLenImpl<std::uint32_t>;
/// 詰み／不詰手数（`MateLen` の16ビット版）

/**
 * @brief 詰み／不詰手数（`MateLen` の16ビット版）
 *
 * 実はエンジン全体で `MateLen16` を使ってもパフォーマンス的にはそれほど影響ないのだが、いつか最終局面の駒あまり枚数を
 * 考慮したくなった時に差が出るかもしれないので、型をちゃんと使い分ける。
 */
using MateLen16 = detail::MateLenImpl<std::uint16_t>;

/**
 * @brief 0手詰め／0手不詰（MateLenの最小値）
 */
inline constexpr MateLen kZeroMateLen = MateLen{0};
/**
 * @brief 最大手数の詰み／最大手数の不詰（MateLenの最大値）
 */
inline constexpr MateLen kDepthMaxMateLen = MateLen{kDepthMax};
/**
 * @brief -1手詰め／-1手不詰（範囲外値）
 *
 * `kZeroMateLen` よりも小さな値を表す特別な定数。探索中に用いてはならず、変数の初期値としてのみ用いる。配列の中から
 * 最も大きな詰み手数を調べたい場合に用いる。
 *
 * ```c++
 * MateLen max_len = kMinus1MateLen;
 * for (const auto& len : len_list) {  // len_list 内の値は [kZeroMateLen, kDepthMaxMateLen] の範囲
 *   max_len = std::max(max_len, len);
 * }
 * ```
 */
inline constexpr MateLen kMinus1MateLen = kZeroMateLen - 1;
/**
 * @brief 詰み／不詰手数の最大値 + 1（範囲外値）
 *
 * `kDepthMaxMateLen` よりも大きな値を表す特別な定数。探索中に用いてはならず、変数の初期値としてのみ用いる。
 * 詳しくは `kMinus1MateLen` も参照。
 */
inline constexpr MateLen kDepthMaxPlus1MateLen = kDepthMaxMateLen + 1;

/// `kZeroMateLen` の 16 ビット版
inline constexpr MateLen16 kZeroMateLen16 = MateLen16{kZeroMateLen};
/// `kDepthMaxMateLen` の 16 ビット版
inline constexpr MateLen16 kDepthMaxMateLen16 = MateLen16{kDepthMaxMateLen};
/// `kMinus1MateLen` の 16 ビット版
inline constexpr MateLen16 kMinus1MateLen16 = MateLen16{kMinus1MateLen};
/// `kDepthMaxPlus1MateLen` の 16 ビット版
inline constexpr MateLen16 kDepthMaxPlus1MateLen16 = MateLen16{kDepthMaxPlus1MateLen};

}  // namespace komori

#endif  // KOMORI_MATE_LEN_HPP_
