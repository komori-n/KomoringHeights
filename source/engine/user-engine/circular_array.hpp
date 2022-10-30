/**
 * @file circular_array.hpp
 */
#ifndef KOMORI_CIRCULAR_ARRAY_HPP_
#define KOMORI_CIRCULAR_ARRAY_HPP_

#include <array>
#include <type_traits>

#include "typedefs.hpp"

namespace komori {
/**
 * @brief 長さ `N` 、型 `T` の循環配列。
 * @tparam T 格納する値の型
 * @tparam N 循環配列の長さ（`N` > 0）
 *
 * 添字が mod `N` で循環する配列。例えば、 `i`, `i+N`, `i+2*N`, ... へのアクセスはすべて等価。
 */
template <typename T, std::size_t N>
class CircularArray {
  static_assert(N > 0, "N must be positive");

 public:
  /** `i % N` 番目の値を取得する。 */
  constexpr T& operator[](const std::size_t i) noexcept { return data_[i % N]; }
  /** `i % N` 番目の値を取得する。 */
  constexpr const T& operator[](const std::size_t i) const noexcept { return data_[i % N]; }

  /**
   * @brief 全要素をデフォルトコンストラクタで初期化する。
   *
   * `T` がデフォルト構築可能な時のみ定義する。
   */
  template <typename U = T,
            Constraints<std::enable_if_t<std::is_same_v<T, U> && std::is_default_constructible_v<U>>> = nullptr>
  constexpr void Clear() noexcept(std::is_nothrow_default_constructible_v<U>) {
    for (auto& v : data_) {
      v = T{};
    }
  }

 private:
  /** Actual data array */
  std::array<T, N> data_;
};
}  // namespace komori

#endif  // KOMORI_CIRCULAR_ARRAY_HPP_
