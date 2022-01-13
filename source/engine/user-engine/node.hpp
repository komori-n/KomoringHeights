#ifndef NODE_HPP_
#define NODE_HPP_

#include <stack>

#include "hands.hpp"
#include "node_history.hpp"
#include "path_keys.hpp"
#include "typedefs.hpp"

namespace komori {
class Node {
 public:
  explicit Node(Position& n, bool or_node, Key path_key = 0, Depth depth = 0)
      : n_{n}, or_color_{or_node ? n.side_to_move() : ~n.side_to_move()}, depth_{depth}, path_key_{path_key} {}

  /// 探索履歴を削除した node を作成する。局面の参照は内部で共有しているので、片方の局面を動かすともう片方の局面も
  /// 変わってしまうので注意。
  Node HistoryClearedNode() { return Node{n_, or_color_ == n_.side_to_move(), path_key_, depth_}; }

  void DoMove(Move m) {
    path_key_ = PathKeyAfter(m);
    node_history_.Visit(n_.state()->board_key(), this->OrHand());
    n_.do_move(m, st_info_.emplace());
    depth_++;
    move_count_++;
  }

  void UndoMove(Move m) {
    depth_--;
    n_.undo_move(m);
    st_info_.pop();
    node_history_.Leave(n_.state()->board_key(), this->OrHand());
    path_key_ = PathKeyBefore(path_key_, m, depth_);
  }

  Hand OrHand() const { return n_.hand_of(or_color_); }
  Hand AndHand() const { return n_.hand_of(~or_color_); }
  Hand OrHandAfter(Move move) const {
    if (IsOrNode()) {
      return AfterHand(n_, move, OrHand());
    } else {
      return OrHand();
    }
  }

  bool IsRepetition() const { return node_history_.Contains(n_.state()->board_key(), this->OrHand()); }

  bool IsRepetitionAfter(Move move) const {
    return node_history_.Contains(n_.board_key_after(move), this->OrHandAfter(move));
  }

  bool IsRepetitionOrInferior() const {
    auto node_state = node_history_.State(n_.state()->board_key(), this->OrHand());
    return node_state == NodeHistory::NodeState::kRepetitionOrInferior;
  }

  bool IsRepetitionOrInferiorAfter(Move move) const {
    auto node_state = node_history_.State(n_.board_key_after(move), this->OrHandAfter(move));
    return node_state == NodeHistory::NodeState::kRepetitionOrInferior;
  }

  bool IsExceedLimit(Depth max_depth) const { return depth_ >= max_depth; }

  auto& Pos() { return n_; }
  auto& Pos() const { return n_; }
  Depth GetDepth() const { return depth_; }
  Key GetPathKey() const { return path_key_; }
  Key PathKeyAfter(Move m) const { return ::komori::PathKeyAfter(path_key_, m, depth_); }
  std::uint64_t GetMoveCount() const { return move_count_; }
  bool IsOrNode() const { return n_.side_to_move() == or_color_; }

 private:
  Position& n_;
  Color or_color_;
  Depth depth_{};
  NodeHistory node_history_{};
  std::stack<StateInfo> st_info_{};
  Key path_key_{};
  std::uint64_t move_count_{};
};

/// 局面 n から moves で手を一気に進める。nに対し、moves の前から順に n.DoMove(m) を適用する。
inline void RollForward(Node& n, const std::vector<Move>& moves) {
  for (const auto& move : moves) {
    n.DoMove(move);
  }
}

/// 局面 n から moves で手を一気に戻す。n に対し、moves の後ろから順に n.UndoMove(m) を適用する。
inline void RollBack(Node& n, const std::vector<Move>& moves) {
  for (auto itr = moves.crbegin(); itr != moves.crend(); ++itr) {
    n.UndoMove(*itr);
  }
}
}  // namespace komori
#endif  // NODE_HPP_