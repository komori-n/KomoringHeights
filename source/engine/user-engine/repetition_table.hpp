/**
 * @file repetition_table.hpp
 */
#ifndef KOMORI_REPETITION_TABLE_HPP_
#define KOMORI_REPETITION_TABLE_HPP_

#include <iostream>
#include <limits>
#include <vector>

#include "spin_lock.hpp"
#include "typedefs.hpp"

namespace komori::tt {
/**
 * @brief 千日手手順（経路ハッシュ値）を記録する置換表
 *
 * `std::unordered_map<std::pair<Key, Depth>>` のような機能を実装するが、内部はただの配列（std::vector）で
 * 実装されている。ハッシュ値が衝突したときは、線形走査により格納するインデックスを求める。
 *
 * LookUp速度を高速に保つために、置換表の高々 30% しか要素を格納しない。メモリ使用率が 30% を超えた場合、
 * Garbage Collectionにより古いエントリを消す。
 *
 * @note `std::unordered_map` を用いるより、`std::vector` + 線形走査法をしたほうが `Containis()` の速度が 20% ほど
 *       高速化できる。ただし、`Insert()` の速度が 20% ほど遅くなっているので注意。実用上は、
 *       `Insert()` の回数よりも `Contains()` で検索する回数の方が多いと考えられるので、全体としては早くなっているはず。
 */
class RepetitionTable {
 public:
  /// 置換表世代。メモリ量をケチるために32 bitsで持つ。オーバーフローに注意。
  using Generation = std::uint32_t;

  /**
   * @brief Construct a new Repetition Table object
   *
   * @param table_size 置換表サイズ
   */
  explicit RepetitionTable(std::size_t table_size = 1) { Resize(table_size); }
  /// Copy constructor(delete)
  RepetitionTable(const RepetitionTable&) = delete;
  /// Move constructor(delete)
  RepetitionTable(RepetitionTable&&) noexcept = delete;
  /// Copy assign operator(delete)
  RepetitionTable& operator=(const RepetitionTable&) = delete;
  /// Move assign operator(delete)
  RepetitionTable& operator=(RepetitionTable&&) noexcept = delete;
  /// Destructor(default)
  ~RepetitionTable() = default;

  /// 置換表に保存された経路ハッシュ値をすべて削除する。
  void Clear() {
    generation_ = 0;
    entry_count_ = 0;

    next_generation_update_ = entries_per_generation_;
    next_gc_ = kInitialGcDuration;

    const TableEntry initial_entry{kEmptyKey, 0, 0};
    std::fill(hash_table_.begin(), hash_table_.end(), initial_entry);
  }

  /**
   * @brief 置換表サイズを `table_size` へ変更する。
   *
   * @param table_size 置換表サイズ
   *
   * 置換表サイズを `table_size` へと変更する。もし `Size() == table_size` ならば、何もしない。
   * `Size() != table_size` なら置換表のりサイズと `Clear()` を行う。
   */
  void Resize(std::size_t table_size) {
    if (Size() != table_size) {
      table_size = std::max<decltype(table_size)>(table_size, 1);
      entries_per_generation_ = std::max<std::size_t>(table_size / kGenerationPerTableSize, 1);
      hash_table_.resize(table_size);
      hash_table_.shrink_to_fit();
      Clear();
    }
  }

  /**
   * @brief 経路ハッシュ値 `path_key` に千日手判定開始深さ `depth` を設定する
   * @param path_key 経路ハッシュ値
   * @param depth    千日手判定開始深さ
   */
  void Insert(Key path_key, Depth depth) {
    std::lock_guard lock(lock_);
    auto index = StartIndex(path_key);
    while (hash_table_[index].key != kEmptyKey && hash_table_[index].key != path_key) {
      index = Next(index);
    }

    if (hash_table_[index].key == kEmptyKey) {
      hash_table_[index] = TableEntry{path_key, depth, generation_};
      entry_count_++;
      if (entry_count_ >= next_generation_update_) {
        generation_++;
        next_generation_update_ = entry_count_ + entries_per_generation_;
        if (generation_ >= next_gc_) {
          CollectGarbage();
          next_gc_ = generation_ + kGcDuration;
        }
      }
    } else {
      hash_table_[index].depth = std::max(depth, hash_table_[index].depth);
      hash_table_[index].generation = generation_;
    }
  }

  /**
   * @brief 経路ハッシュ値 `path_key` が保存されているかどうか判定する。
   * @param path_key 経路ハッシュ値
   * @return `path_key` が保存されていればその深さ、なければ `std::nullopt`
   */
  std::optional<Depth> Contains(Key path_key) const {
    std::lock_guard lock(lock_);
    for (auto index = StartIndex(path_key); hash_table_[index].key != kEmptyKey; index = Next(index)) {
      if (hash_table_[index].key == path_key) {
        return {hash_table_[index].depth};
      }
    }

    return std::nullopt;
  }

  std::size_t Size() const { return hash_table_.size(); }

  /// 置換表のメモリ使用率を求める。
  double HashRate() const {
    const auto prev_gc = (next_gc_ - kGcKeepGeneration - kGcDuration);
    const auto num_entries =
        (generation_ - prev_gc) * entries_per_generation_ + (entry_count_ % entries_per_generation_);

    return static_cast<double>(num_entries) / static_cast<double>(Size());
  }

  Generation GetGeneration() const { return generation_; }

 private:
  /// 置換表全体を何 generation で管理するか
  static constexpr std::uint64_t kGenerationPerTableSize = 20;
  /// 初回のGCタイミング
  static constexpr Generation kInitialGcDuration = 6;
  /// 2回目以降のGCタイミング
  static constexpr Generation kGcDuration = 3;
  /// GCで残す置換表世代数
  static constexpr Generation kGcKeepGeneration = 3;
  /// 空を表す経路ハッシュ値。`kEmptyKey` ではなく 0 を用いることで、`Clear()` が倍近く高速化できる。
  static constexpr Key kEmptyKey = 0;

  /// 置換表に格納するエントリ。16 bits に詰める。
  struct TableEntry {
    Key key;                ///< 経路ハッシュ値。使用していないなら kEmptyKey。
    Depth depth;            ///< 探索深さ
    Generation generation;  ///< 置換表世代
  };
  static_assert(sizeof(TableEntry) == 16);

  /// `path_key` に対する探索開始インデックスを求める。
  std::size_t StartIndex(Key path_key) const {
    // Stockfishのアイデア。`path_key` が std::uint64_t 上の一様分布に従うとき、
    // 乗算とシフトにより [0, hash_table_.size()) 上の一様分布に従う変数に変換できる。
    const Key key_low = path_key & Key{0xffff'ffffULL};
    return static_cast<std::size_t>((key_low * hash_table_.size()) >> 32);
  }

  /// `index` の次のインデックスを求める。
  std::size_t Next(std::size_t index) const {
    // index = (index + 1) % hash_table_.size() と書くよりも if 文を用いた方が有意に速い。
    if (index + 1 >= hash_table_.size()) {
      return 0;
    } else {
      return index + 1;
    }
  }

  /**
   * @brief ガベージコレクションを行う
   *
   * 現在の置換表世代 `generation_` から `kGcKeepGeneration` 世代前のエントリまでを残し、
   * それより古いエントリを削除する。また、歯抜けエントリがあると線形走査法で正しく探索できなくなるので、
   * エントリをできるだけ手前に詰める。（コンパクション）
   */
  void CollectGarbage() {
    // [erased_generation, generation_] の範囲のエントリだけを残す。
    const auto erased_generation = generation_ - kGcKeepGeneration;

    // 対象entryのgenerationが [erased_generation, generation_] の範囲内ならfalse、それ以外ならtrue
    const auto should_erase = [erased_generation, this](const TableEntry& entry) {
      const auto entry_generation = entry.generation;

      // erased_generationとerased_generationの間にstd::uint32_t::maxの境界をまたいでいる可能性があるので注意
      if (erased_generation < generation_) {
        return entry_generation < erased_generation || generation_ < entry_generation;
      } else {
        return generation_ < entry_generation && entry_generation < erased_generation;
      }
    };

    for (auto& entry : hash_table_) {
      if (entry.key != kEmptyKey && should_erase(entry)) {
        entry.key = kEmptyKey;
      }
    }

    // コンパクション。配列の後ろの方で微妙に歯抜けができてエントリにアクセスできなくなる可能性があるが目をつぶる。
    for (auto& entry : hash_table_) {
      if (entry.key == kEmptyKey) {
        continue;
      }

      for (auto index = StartIndex(entry.key); &hash_table_[index] != &entry; index = Next(index)) {
        if (hash_table_[index].key == kEmptyKey) {
          hash_table_[index] = entry;
          entry.key = kEmptyKey;
          break;
        }
      }
    }
  }

  mutable SpinLock lock_{};      ///< 排他ロック
  Generation generation_{};      ///< 現在の置換表世代
  std::uint64_t entry_count_{};  ///< 現在までにInsert()したエントリ数

  std::uint64_t next_generation_update_{};  ///< 次回generation_をインクリメントするタイミング
  Generation next_gc_{};                    ///< 次回GCを行うGeneration
  std::uint64_t entries_per_generation_{};  ///< 1 generationあたりのエントリ数
  std::vector<TableEntry> hash_table_{};    ///< 置換表本体
};
}  // namespace komori::tt

#endif  // KOMORI_REPETITION_TABLE_HPP_
