#ifndef BITSET_HPP_
#define BITSET_HPP_

#include <numeric>
#include <type_traits>

namespace komori {
template <typename T>
class BitSet {
 public:
  using ValueType = T;
  static_assert(std::is_integral_v<ValueType> && std::is_unsigned_v<ValueType>);

  constexpr explicit BitSet(ValueType val) : val_(val) {}
  constexpr BitSet() = default;
  constexpr BitSet(const BitSet&) = default;
  constexpr BitSet(BitSet&&) noexcept = default;
  constexpr BitSet& operator=(const BitSet&) = default;
  constexpr BitSet& operator=(BitSet&&) noexcept = default;
  ~BitSet() = default;

  static constexpr BitSet Full() { return BitSet{std::numeric_limits<ValueType>::max()}; }

  constexpr BitSet& Set(std::size_t i) {
    if (i < sizeof(ValueType) * kBitPerByte) {
      val_ |= ValueType{1} << i;
    }
    return *this;
  }

  constexpr BitSet& Reset(std::size_t i) {
    if (i < sizeof(ValueType) * kBitPerByte) {
      val_ &= ~(ValueType{1} << i);
    }
    return *this;
  }

  constexpr bool Test(std::size_t i) const {
    if (i < sizeof(ValueType) * kBitPerByte) {
      return (val_ & (ValueType{1} << i)) != ValueType{0};
    }
    return false;
  }

  constexpr ValueType Value() const { return val_; }

 private:
  static constexpr inline std::size_t kBitPerByte = 8;

  ValueType val_{};
};

using BitSet64 = BitSet<std::uint64_t>;
}  // namespace komori

#endif  // BITSET_HPP_