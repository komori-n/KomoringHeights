#ifndef KOMORI_CIRCULAR_ARRAY_HPP_
#define KOMORI_CIRCULAR_ARRAY_HPP_

#include <array>
#include <type_traits>

namespace komori {
template <typename T, std::size_t N>
class CircularArray {
  static_assert(N > 0, "N must be positive");

 public:
  constexpr T& operator[](const std::size_t i) noexcept { return data_[i % N]; }
  constexpr const T& operator[](const std::size_t i) const noexcept { return data_[i % N]; }

  template <typename U = T,
            std::enable_if_t<std::is_same_v<T, U> && std::is_default_constructible_v<U>, std::nullptr_t> = nullptr>
  constexpr void Clear() noexcept(std::is_nothrow_default_constructible_v<U>) {
    for (auto& v : data_) {
      v = T{};
    }
  }

 private:
  std::array<T, N> data_;
};
}  // namespace komori

#endif  // KOMORI_CIRCULAR_ARRAY_HPP_
