/**
 * @file visit_history.hpp
 */
#ifndef KOMORI_VISIT_HISTORY_HPP_
#define KOMORI_VISIT_HISTORY_HPP_

#include <optional>
#include <unordered_map>

#include "ranges.hpp"

namespace komori {
/**
 * @brief 探索履歴を管理し、千日手や優等局面の判定を行うクラス。
 *
 * df-pn探索中に、親ノードで訪れた局面と同一局面や優等局面／劣等局面で探索を打ち切りたいことがある。
 * このクラスは、これまでに訪れた (`board_key`, `hand`) の組と探索深さ `depth` を管理し、親ノードで類似局面が
 * あったかどうかを判定する。
 *
 * `Visit()` で新たな局面に訪れ、`Leave()` で訪れた局面の削除ができる。
 */
class VisitHistory {
 public:
  /**
   * @brief (`board_key`, `hand`) を履歴に登録する
   * @param board_key   局面のハッシュ
   * @param hand        攻め方の持ち駒
   * @param depth       現在の探索深さ
   *
   * `Contains(board_key, hand) == true` の場合、呼び出し禁止。
   */
  void Visit(Key board_key, Hand hand, Depth depth) { visited_.emplace(board_key, std::make_pair(hand, depth)); }

  /**
   * @brief (`board_key`, `hand`) を履歴から消す
   * @param board_key   局面のハッシュ
   * @param hand        攻め方の持ち駒
   * @param depth       現在の探索深さ
   *
   * (`board_key`, `hand`) は必ず `Visit()` で登録された局面でなければならない。
   */
  void Leave(Key board_key, Hand hand, Depth /* depth */) {
    auto [begin, end] = visited_.equal_range(board_key);
    for (auto itr = begin; itr != end; ++itr) {
      if (itr->second.first == hand) {
        visited_.erase(itr);
        return;
      }
    }
  }

  /**
   * @brief (`board_key`, `hand`) の同一局面が履歴に記録されているか調べる。
   * @param board_key 盤面のハッシュ
   * @param hand      攻め方の持ち駒
   */
  std::optional<Depth> Contains(Key board_key, Hand hand) const {
    const auto range = visited_.equal_range(board_key);
    for (const auto& [bk, history_hd] : AsRange(range)) {  // NOLINT(readability-use-anyofallof)
      const auto& [history_hand, history_depth] = history_hd;
      if (history_hand == hand) {
        return history_depth;
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
    const auto range = visited_.equal_range(board_key);
    for (const auto& [bk, history_hd] : AsRange(range)) {  // NOLINT(readability-use-anyofallof)
      const auto& [history_hand, history_depth] = history_hd;
      if (hand_is_equal_or_superior(history_hand, hand)) {
        return history_depth;
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
    const auto range = visited_.equal_range(board_key);
    for (const auto& [bk, history_hd] : AsRange(range)) {  // NOLINT(readability-use-anyofallof)
      const auto& [history_hand, history_depth] = history_hd;
      if (hand_is_equal_or_superior(hand, history_hand)) {
        return history_depth;
      }
    }

    return std::nullopt;
  }

 private:
  /// 攻め方の持ち駒と探索深さのペア
  using HandDepthPair = std::pair<Hand, Depth>;
  /// 経路上で訪れたことがある局面一覧。局面の優等性を利用したいためmultisetを用いる。
  std::unordered_multimap<Key, HandDepthPair> visited_;
};
}  // namespace komori

#endif  // KOMORI_VISIT_HISTORY_HPP_
