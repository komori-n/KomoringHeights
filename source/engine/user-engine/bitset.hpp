#ifndef KOMORI_BITSET_HPP_
#define KOMORI_BITSET_HPP_

#include <cstdint>
#include <limits>
#include <numeric>
#include <type_traits>

namespace komori {
template <typename T>
class BitSet {
 public:
  using ValueType = T;
  static_assert(std::is_integral_v<ValueType> && std::is_unsigned_v<ValueType>);

  constexpr explicit BitSet(ValueType val) noexcept : val_(val) {}
  constexpr BitSet() noexcept = default;
  constexpr BitSet(const BitSet&) noexcept = default;
  constexpr BitSet(BitSet&&) noexcept = default;
  constexpr BitSet& operator=(const BitSet&) noexcept = default;
  constexpr BitSet& operator=(BitSet&&) noexcept = default;
  ~BitSet() = default;

  static constexpr BitSet Full() noexcept { return BitSet{std::numeric_limits<ValueType>::max()}; }

  constexpr BitSet& Set(std::size_t i) noexcept {
    if (i < sizeof(ValueType) * kBitPerByte) {
      val_ |= ValueType{1} << i;
    }
    return *this;
  }

  constexpr BitSet& Reset(std::size_t i) noexcept {
    if (i < sizeof(ValueType) * kBitPerByte) {
      val_ &= ~(ValueType{1} << i);
    }
    return *this;
  }

  constexpr bool Test(std::size_t i) const noexcept {
    if (i < sizeof(ValueType) * kBitPerByte) {
      return (val_ & (ValueType{1} << i)) != ValueType{0};
    }
    return false;
  }

  constexpr bool operator[](std::size_t i) const noexcept { return Test(i); }

  constexpr ValueType Value() const noexcept { return val_; }

 private:
  static constexpr inline std::size_t kBitPerByte = CHAR_BIT;

  ValueType val_{};
};

using BitSet64 = BitSet<std::uint64_t>;
}  // namespace komori

#endif  // KOMORI_BITSET_HPP_
