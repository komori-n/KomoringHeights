#ifndef KOMORI_BITSET_HPP_
#define KOMORI_BITSET_HPP_

#include <cstdint>
#include <limits>
#include <numeric>
#include <type_traits>

namespace komori {
/**
 * @brief 整数 `T` を用いたフラグ集合。
 * @tparam T 内部で用いる整数型（unsigned）
 *
 * `std::uint64_t` のような固定幅整数を用いた2値フラグの集合。 `T` が `N` ビット変数の場合、 `N` 個の `true`/`false`
 * フラグを格納できる。 状態の変更は `Set()`/`Reset()` 、状態の取得は `Test()` または `operator[]` を用いる。
 *
 * 使える添字は 0-base。例えば、`T=uint64_t` なら [0, 64) の範囲。ただし、範囲外はすべて`false`
 * として扱う。すなわち、例えば `T=uint64_t` の場合、 `Set(334)` は何もせず、 `Test(334)` は常に `false` を返す。
 */
template <typename T>
class BitSet {
 public:
  /** フラグ格納に用いる型。 */
  using ValueType = T;
  /* `T` が unsigned integer ではない場合はコンパイルエラーにする。 */
  static_assert(std::is_integral_v<ValueType> && std::is_unsigned_v<ValueType>);

  /** フラグをすべて `true` にした `BitSet` を返す。 */
  static constexpr BitSet Full() noexcept { return BitSet{std::numeric_limits<ValueType>::max()}; }

  /** 生値 `val` から初期化する。 */
  constexpr explicit BitSet(ValueType val) noexcept : val_(val) {}
  constexpr BitSet() noexcept = default;
  constexpr BitSet(const BitSet&) noexcept = default;
  constexpr BitSet(BitSet&&) noexcept = default;
  constexpr BitSet& operator=(const BitSet&) noexcept = default;
  constexpr BitSet& operator=(BitSet&&) noexcept = default;
  ~BitSet() = default;

  /**
   * @brief `i` 番目のフラグを `true` に設定する。
   * @param i         添字
   * @return BitSet&  `*this`
   *
   * `i` が範囲外を指している場合、なにもしない。
   */
  constexpr BitSet& Set(std::size_t i) noexcept {
    if (i < kBitNum) {
      val_ |= ValueType{1} << i;
    }
    return *this;
  }

  /**
   * @brief `i` 番目のフラグを `false` に設定する。
   * @param i         添字
   * @return BitSet&  `*this`
   *
   * `i` が範囲外を指している場合、なにもしない。
   */
  constexpr BitSet& Reset(std::size_t i) noexcept {
    if (i < kBitNum) {
      val_ &= ~(ValueType{1} << i);
    }
    return *this;
  }

  /**
   * @brief `i` 番目のフラグ状態を取得する。
   * @param i      添字
   * @return bool  `i` 番目のフラグ状態
   *
   * `i` が範囲外を指している場合、 `false` を返す。
   */
  constexpr bool Test(std::size_t i) const noexcept {
    if (i < kBitNum) {
      return (val_ & (ValueType{1} << i)) != ValueType{0};
    }
    return false;
  }

  /**
   * @brief `i` 番目のフラグ状態を取得する。
   * @param i      添字
   * @return bool  `i` 番目のフラグ状態
   *
   * `i` が範囲外を指している場合、 `false` を返す。
   */
  constexpr bool operator[](std::size_t i) const noexcept { return Test(i); }

  /** クラス内部の整数の生値を返す。 */
  constexpr ValueType Value() const noexcept { return val_; }

 private:
  /** このクラスで格納できるフラグ数。 */
  static constexpr inline std::size_t kBitNum = sizeof(ValueType) * CHAR_BIT;

  /** フラグの生値を格納する整数。添字 0, 1, ... は下位ビットから順に bit0, bit1, ... へと対応する。 */
  ValueType val_{};
};

/** 64ビット整数を用いたフラグ集合。よく使う（というかこれしか使わない）のでここで型定義しておく。 */
using BitSet64 = BitSet<std::uint64_t>;
}  // namespace komori

#endif  // KOMORI_BITSET_HPP_
