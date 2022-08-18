#ifndef KOMORI_VISIT_HISTORY_HPP_
#define KOMORI_VISIT_HISTORY_HPP_

#include <unordered_map>

#include "typedefs.hpp"

namespace komori {
/**
 * @brief 探索履歴を管理し、千日手や優等局面の判定を行うクラス。
 *
 * df-pn探索中に、親ノードで訪れた局面と同一局面や優等局面／劣等局面で探索を打ち切りたいことがある。
 * このクラスは、これまでに訪れた (`board_key`, `hand`) の組を管理し、親ノードで類似局面があったかどうかを判定する。
 *
 * `Visit()` で新たな局面に訪れ、`Leave()` で訪れた局面の削除ができる。
 *
 * @note 詰将棋探索では手数が長くなりがちなので、基本的には `Position()` の千日手判定よりも高速に動作する。
 */
class VisitHistory {
 public:
  /**
   * @brief (`board_key`, `hand`) を履歴に登録する
   * @param board_key   局面のハッシュ
   * @param hand        攻め方の持ち駒
   *
   * `Contains(board_key, hand) == true` の場合、呼び出し禁止。
   */
  void Visit(Key board_key, Hand hand) { visited_.emplace(board_key, hand); }

  /**
   * @brief (`board_key`, `hand`) を履歴から消す
   * @param board_key   局面のハッシュ
   * @param hand        攻め方の持ち駒
   *
   * (`board_key`, `hand`) は必ず `Visit()` で登録された局面でなければならない。
   */
  void Leave(Key board_key, Hand hand) {
    auto [begin, end] = visited_.equal_range(board_key);
    for (auto itr = begin; itr != end; ++itr) {
      if (itr->second == hand) {
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
  bool Contains(Key board_key, Hand hand) const {
    auto [begin, end] = visited_.equal_range(board_key);

    for (auto itr = begin; itr != end; ++itr) {
      auto history_hand = itr->second;
      if (history_hand == hand) {
        return true;
      }
    }

    return false;
  }

  /// `board_key` の同一局面が履歴に記録されているか調べる。
  bool Contains(Key board_key) const { return visited_.find(board_key) != visited_.end(); }

  /**
   * @brief (`board_key`, `hand`) の優等局面が履歴に記録されているか調べる。
   * @param board_key   盤面ハッシュ
   * @param hand        攻め方の持ち駒
   */
  bool IsInferior(Key board_key, Hand hand) const {
    auto [begin, end] = visited_.equal_range(board_key);

    for (auto itr = begin; itr != end; ++itr) {
      auto history_hand = itr->second;
      if (hand_is_equal_or_superior(history_hand, hand)) {
        return true;
      }
    }

    return false;
  }

  /**
   * @brief (`board_key`, `hand`) の劣等局面が履歴に記録されているか調べる。
   * @param board_key   盤面ハッシュ
   * @param hand        攻め方の持ち駒
   */
  bool IsSuperior(Key board_key, Hand hand) const {
    auto [begin, end] = visited_.equal_range(board_key);

    for (auto itr = begin; itr != end; ++itr) {
      auto history_hand = itr->second;
      if (hand_is_equal_or_superior(hand, history_hand)) {
        return true;
      }
    }

    return false;
  }

 private:
  /// 経路上で訪れたことがある局面一覧。局面の優等性を利用したいためmultisetを用いる。
  std::unordered_multimap<Key, Hand> visited_;
};
}  // namespace komori

#endif  // KOMORI_VISIT_HISTORY_HPP_
