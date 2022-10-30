/**
 * @file repetition_table.hpp
 */
#ifndef KOMORI_REPETITION_TABLE_HPP_
#define KOMORI_REPETITION_TABLE_HPP_

#include <array>
#include <limits>
#include <unordered_set>

#include "typedefs.hpp"

namespace komori {
/**
 * @brief 千日手手順を記録する置換表
 *
 * 千日手と判明した経路ハッシュ値を高々 `size_max` 個記憶する。もし記録している手順数が `size_max` を超える場合、
 * 古い結果を削除する GC 機能を備えている。
 *
 * @note `std::unordered_set` を複数個持つことでこの GC 機能を実現している。
 */
class RepetitionTable {
 public:
  /// 置換表に保存された経路ハッシュ値をすべて削除する。
  void Clear() {
    for (auto& tbl : keys_) {
      tbl.clear();
    }
  }
  /// 置換表に登録できる key の最大個数を設定する。
  void SetTableSizeMax(std::size_t size_max) { size_max_ = size_max + 1; }

  /// 置換表のうち古くなった部分を削除する。
  void CollectGarbage() {
    // GC 機能は `Insert()` 内部で行っているので、ここですべきことはなにもない
  }

  /// 経路ハッシュ値 `path_key` を登録する
  void Insert(Key path_key) {
    keys_[idx_].insert(path_key);
    if (keys_[idx_].size() >= size_max_ / kTableLen) {
      idx_ = (idx_ + 1) % kTableLen;
      keys_[idx_].clear();
    }
  }

  /// 経路ハッシュ値 `path_key` が保存されているかどうか判定する。
  bool Contains(Key path_key) const {
    return std::any_of(keys_.begin(), keys_.end(), [&](const auto& tbl) { return tbl.find(path_key) != tbl.end(); });
  }

  /// 現在置換表に保存されている経路ハッシュ値の個数をカウントする。
  constexpr std::size_t Size() const {
    std::size_t ret = 0;
    for (auto& tbl : keys_) {
      ret += tbl.size();
    }
    return ret;
  }

  /// 置換表のメモリ使用率を求める。
  double HashRate() const { return static_cast<double>(Size()) / size_max_; }

 private:
  /// 内部で持つ `std::unordered_set` の個数。あまり多いと LookUp 時間が増大する。
  static constexpr inline std::size_t kTableLen = 2;

  /// 経路ハッシュ値置換表の本体
  std::array<std::unordered_set<Key>, kTableLen> keys_{};
  /// 現在アクティブな置換表([0, kTableLen))
  std::size_t idx_{0};
  /// 置換表内に保存できる経路ハッシュ値の最大個数
  std::size_t size_max_{std::numeric_limits<std::size_t>::max()};
};
}  // namespace komori

#endif  // KOMORI_REPETITION_TABLE_HPP_
