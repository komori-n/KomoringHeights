/**
 * @file circular_array.hpp
 */
#ifndef KOMORI_CIRCULAR_ARRAY_HPP_
#define KOMORI_CIRCULAR_ARRAY_HPP_

#include <array>
#include <type_traits>

#include "type_traits.hpp"

namespace komori {
/**
 * @brief 長さ `kSize` 、型 `T` の循環配列。
 * @tparam T 格納する値の型
 * @tparam kSize 循環配列の長さ（`kSize` > 0）
 *
 * 添字が mod `kSize` で循環する配列。例えば、 `i`, `i+kSize`, `i+2*kSize`, ... へのアクセスはすべて等価。
 */
template <typename T, std::size_t kSize>
class CircularArray {
  static_assert(kSize > 0, "kSize must be positive");

 public:
  /** `i % kSize` 番目の値を取得する。 */
  constexpr T& operator[](const std::size_t i) noexcept { return data_[i % kSize]; }
  /** `i % kSize` 番目の値を取得する。 */
  constexpr const T& operator[](const std::size_t i) const noexcept { return data_[i % kSize]; }

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
  std::array<T, kSize> data_;
};
}  // namespace komori

#endif  // KOMORI_CIRCULAR_ARRAY_HPP_
