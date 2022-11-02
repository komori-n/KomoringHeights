/**
 * @file fixed_size_stack.hpp
 */
#ifndef KOMORI_FIXED_SIZE_STACK_HPP_
#define KOMORI_FIXED_SIZE_STACK_HPP_

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace komori {
/**
 * @brief サイズ固定のスタック。
 * @tparam T 保存する要素の型（デフォルト構築可能かつトリビアルデストラクト可能）
 * @tparam kSize 保存可能な添字の最大個数（`kSize`>0）
 * @internal 命名を `StackStack` にしようと思ったけど意味不明なのでやめた
 *
 * `Push()` および `Pop()` により要素を追加および削除ができるスタック。動的メモリ確保は行わず、`std::array` にて
 * 実装されている。
 *
 * スタックは配列の手前から順に詰める形で実現されている。`operator[]` で要素を取得するときに使う添字は、古い順に
 * 0, 1, ... と振られている。同様に、イテレータ (`begin()` `end()`) は、古い順に要素を返す。
 *
 * @note テンプレートパラメータ `kSize` でサイズの上限を指定できるが、高速化のために範囲チェックは一切行っていない。
 */
template <typename T, std::size_t kSize>
class FixedSizeStack {
 public:
  static_assert(kSize > 0, "kSize shall be greater than 0");
  static_assert(std::is_default_constructible_v<T>, "T shall be default constructible");
  static_assert(std::is_trivially_destructible_v<T>, "T shall be trivially destructible");

  /// `val` をスタックに追加する
  constexpr std::uint32_t Push(T val) {
    const auto i = len_++;
    data_[i] = std::move(val);
    return i;
  }
  /// スタックから要素を1つ削除する
  constexpr void Pop() { --len_; }

  /// イテレータの開始位置
  constexpr auto begin() { return data_.begin(); }
  /// イテレータの開始位置
  constexpr auto begin() const { return data_.begin(); }
  /// イテレータの終了位置
  constexpr auto end() { return data_.begin() + len_; }
  /// イテレータの終了位置
  constexpr auto end() const { return data_.begin() + len_; }
  /// スタックに保存されている要素数
  constexpr auto size() const { return len_; }
  /// スタックが空かどうか
  constexpr bool empty() const { return len_ == 0; }
  /// スタックの先頭（最も前に保存した要素）
  constexpr T& front() { return data_[0]; }
  /// スタックの先頭（最も前に保存した要素）
  constexpr const T& front() const { return data_[0]; }
  /// スタックの末尾（最も後に保存した要素）
  constexpr T& back() { return data_[len_ - 1]; }
  /// スタックの末尾（最も後に保存した要素）
  constexpr const T& back() const { return data_[len_ - 1]; }

  /// `i` 番目に追加した要素
  constexpr const T& operator[](std::uint32_t i) const { return data_[i]; }

 private:
  std::array<T, kSize> data_;  ///< スタックを保存する領域
  std::uint32_t len_{0};       ///< スタックに現在格納されている要素数
};
}  // namespace komori

#endif  // KOMORI_FIXED_SIZE_STACK_HPP_
