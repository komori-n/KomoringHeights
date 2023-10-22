/**
 * @file visit_history.hpp
 */
#ifndef KOMORI_VISIT_HISTORY_HPP_
#define KOMORI_VISIT_HISTORY_HPP_

#include <array>
#include <optional>

#include "ranges.hpp"
#include "typedefs.hpp"

namespace komori {
/**
 * @brief 探索履歴を管理し、千日手や優等局面の判定を行うクラス。
 *
 * df-pn探索中に、親ノードで訪れた局面と同一局面や優等局面／劣等局面で探索を打ち切りたいことがある。
 * このクラスは、これまでに訪れた (`board_key`, `hand`) の組と探索深さ `depth` を管理し、親ノードで類似局面が
 * あったかどうかを判定する。
 *
 * `Visit()` で新たな局面に訪れ、`Leave()` で訪れた局面の削除ができる。
 *
 * 探索履歴の保存に特化したクラスなので、`kDepthMax` 個より多くのエントリを保存することは想定していない。
 *
 * @note std::unordered_multimap の実装と比較して、std::array を用いる実装では約2倍高速に動作する
 */
class VisitHistory {
 public:
  /// Construct a new Visit History object
  VisitHistory() {
    for (auto& entry : hash_table_) {
      entry.board_key = kNullKey;
    }
  }

  /// Copy constructor(delete)
  VisitHistory(const VisitHistory&) = delete;
  /// Move constructor(default)
  VisitHistory(VisitHistory&&) noexcept = default;
  /// Copy assign operator(delete)
  VisitHistory& operator=(const VisitHistory&) = delete;
  /// Move assign operator(default)
  VisitHistory& operator=(VisitHistory&&) noexcept = default;
  /// Destructor(default)
  ~VisitHistory() = default;

  /**
   * @brief (`board_key`, `hand`) を履歴に登録する
   * @param board_key   局面のハッシュ
   * @param hand        攻め方の持ち駒
   * @param depth       現在の探索深さ
   *
   * `Contains(board_key, hand) == true` の場合、呼び出し禁止。
   */
  void Visit(Key board_key, Hand hand, Depth depth) {
    auto index = StartIndex(board_key);
    for (; hash_table_[index].board_key != kNullKey; index = Next(index)) {
    }

    hash_table_[index].board_key = board_key;
    hash_table_[index].hand = hand;
    hash_table_[index].depth = depth;
  }

  /**
   * @brief (`board_key`, `hand`) を履歴から消す
   * @param board_key   局面のハッシュ
   * @param hand        攻め方の持ち駒
   * @param depth       現在の探索深さ
   *
   * (`board_key`, `hand`) は必ず `Visit()` で登録された局面でなければならない。
   */
  void Leave(Key board_key, Hand hand, Depth /* depth */) {
    auto index = StartIndex(board_key);
    for (; hash_table_[index].board_key != board_key || hash_table_[index].hand != hand; index = Next(index)) {
    }

    hash_table_[index].board_key = kNullKey;
  }

  /**
   * @brief (`board_key`, `hand`) の同一局面が履歴に記録されているか調べる。
   * @param board_key 盤面のハッシュ
   * @param hand      攻め方の持ち駒
   */
  std::optional<Depth> Contains(Key board_key, Hand hand) const {
    auto index = StartIndex(board_key);
    for (; hash_table_[index].board_key != kNullKey; index = Next(index)) {
      const auto& entry = hash_table_[index];
      if (entry.board_key == board_key && entry.hand == hand) {
        return {entry.depth};
      }
    }

    return std::nullopt;
  }

  /**
   * @brief (`board_key`, `hand`) の優等局面が履歴に記録されているか調べる。
   * @param board_key   盤面ハッシュ
   * @param hand        攻め方の持ち駒
   */
  std::optional<Depth> IsInferior(Key board_key, Hand hand) const {
    auto index = StartIndex(board_key);
    for (; hash_table_[index].board_key != kNullKey; index = Next(index)) {
      const auto& entry = hash_table_[index];
      if (entry.board_key == board_key && hand_is_equal_or_superior(entry.hand, hand)) {
        return {entry.depth};
      }
    }

    return std::nullopt;
  }

  /**
   * @brief (`board_key`, `hand`) の劣等局面が履歴に記録されているか調べる。
   * @param board_key   盤面ハッシュ
   * @param hand        攻め方の持ち駒
   */
  std::optional<Depth> IsSuperior(Key board_key, Hand hand) const {
    auto index = StartIndex(board_key);
    for (; hash_table_[index].board_key != kNullKey; index = Next(index)) {
      const auto& entry = hash_table_[index];
      if (entry.board_key == board_key && hand_is_equal_or_superior(hand, entry.hand)) {
        return {entry.depth};
      }
    }

    return std::nullopt;
  }

 private:
  /// HashTable のテーブルサイズ。2のべき乗かつ `kDepthMax` 以上の値にしなければならない
  static constexpr std::size_t kTableSize = 4096 * 8;
  /// テーブルにアクセスするための添字に対するマスク。
  static constexpr std::size_t kTableIndexMask = kTableSize - 1;

  static_assert((kTableSize & (kTableSize - 1)) == 0);
  static_assert(kTableSize >= kDepthMax);

  /// ハッシュテーブルに格納するエントリ。16 bits に詰める。
  struct TableEntry {
    Key board_key;  ///< 盤面ハッシュ値。使用していないなら kNullKey。
    Hand hand;      ///< 攻め方の持ち駒。
    Depth depth;    ///< 探索深さ
  };
  static_assert(sizeof(TableEntry) == 16);

  /// `board_key` に対する探索開始インデックスを求める
  constexpr std::size_t StartIndex(Key board_key) const noexcept { return (board_key >> 32) & kTableIndexMask; }
  /// `index` の次のインデックスを求める
  constexpr std::size_t Next(std::size_t index) const noexcept { return (index + 1) & kTableIndexMask; }

  /// ハッシュテーブル本体
  alignas(64) std::array<TableEntry, kTableSize> hash_table_;
};
}  // namespace komori

#endif  // KOMORI_VISIT_HISTORY_HPP_
