#ifndef NODE_HISTORY_HPP_
#define NODE_HISTORY_HPP_

#include <unordered_map>

#include "typedefs.hpp"

namespace komori {
/**
 * @brief 探索履歴を管理し、千日手や優等局面の判定を行うクラス。
 *
 * df-pn探索中に、親ノードで訪れた局面と同一局面や優等局面／劣等局面で探索を打ち切りたいことがある。
 * このクラスは、これまでに訪れた (board_key, hand) の組を管理し、親ノードで類似局面があったかどうかを判定する。
 *
 * ## コード例
 *
 * ```
 *    void dfpn(Position& n, NodeHistory& node_history, ...) {
 *        auto board_key = n.board_key();
 *        auto hand = n.or_hand();
 *        auto state = node_history.State(board_key, hand);
 *
 *        // state を用いた千日手・劣等ループの判定...
 *
 *        node_history.Visit(board_key, hand);
 *
 *        // 子ノードの探索...
 *
 *        node_history.Leave(board_key, hand);
 *    }
 * ```
 */
class NodeHistory {
 public:
  enum class NodeState {
    kFirst,                 ///< 置換表に類似局面の登録なし
    kRepetitionOrInferior,  ///< 置換表に同一局面または優等局面の登録あり
  };

  /**
   * @brief (board_key, hand) の同一／優等局面が履歴に記録されているか調べる
   *
   * 同一局面または優等局面が登録されていれば true を返す。
   *
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

  /**
   * @brief (board_key, hand) の同一局面が履歴に記録されているか調べる。
   *
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

  /**
   * @brief (board_key, hand) を履歴に登録する
   *
   * State(board_key, hand) == NodeState::kRepetition の場合、呼び出し禁止。
   *
   * @param board_key   局面のハッシュ
   * @param hand        攻め方の持ち駒
   */
  void Visit(Key board_key, Hand hand) { visited_.emplace(board_key, hand); }

  /**
   * @brief (board_key, hand) を履歴から消す
   *
   * (board_key, hand) は必ず Visit() で登録された局面でなければならない。
   *
   * @param board_key   局面のハッシュ
   * @param hand        攻め方の持ち駒
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

 private:
  std::unordered_multimap<Key, Hand> visited_;
};
}  // namespace komori

#endif  // NODE_HISTORY_HPP_
