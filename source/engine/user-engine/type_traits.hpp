/**
 * @file type_traits.hpp
 */
#ifndef KOMORI_TYPE_TRAITS_HPP_
#define KOMORI_TYPE_TRAITS_HPP_

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

namespace detail {
/**
 * @brief `Args...` を無視して`Type` に `std::nullptr_t` を定義するメタ関数
 *
 * 詳しくは `Constraints` のコメントを参照。
 */
template <typename... Args>
struct ConstraintsImpl {
  using Type = std::nullptr_t;  ///< `Args...` を無視して `std::nullptr_t` を定義する
};
}  // namespace detail

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
using Constraints = typename detail::ConstraintsImpl<Args...>::Type;

// template 関数のカバレッジ計測は大変なのでやらない
// LCOV_EXCL_START

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
 * @brief `operator<` から各種演算子を自動定義するクラス。
 * @tparam T `operator<` が定義されたクラス
 *
 * `operator<` の定義から以下の演算子を自動定義する。
 *
 * - `operator<=`
 * - `operator>`
 * - `operator>=`
 *
 * 詳しくは `DefineNotEqualByEauyl` も参照のこと。
 */
template <typename T>
struct DefineComparisonOperatorsByLess {
  /// `lhs <= rhs` を判定する（`operator<` からの自動定義）
  constexpr friend bool operator<=(const T& lhs, const T& rhs) noexcept(noexcept(lhs < rhs)) { return !(rhs < lhs); }
  /// `lhs > rhs` を判定する（`operator==` および `operator<` からの自動定義）
  constexpr friend bool operator>(const T& lhs, const T& rhs) noexcept(noexcept(rhs < lhs)) { return rhs < lhs; }
  /// `lhs >= rhs` を判定する（`operator==` および `operator<` からの自動定義）
  constexpr friend bool operator>=(const T& lhs, const T& rhs) noexcept(noexcept(lhs < rhs)) { return !(lhs < rhs); }
};

// LCOV_EXCL_STOP

}  // namespace komori

#endif  // KOMORI_TYPE_TRAITS_HPP_
