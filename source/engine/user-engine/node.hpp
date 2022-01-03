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
  explicit Node(Position& n, Key path_key = 0, Depth depth = 0)
      : n_{n}, or_color_{n.side_to_move()}, depth_{depth}, path_key_{path_key} {}

  Node NewInstance() { return Node{n_, path_key_, depth_}; }

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

  bool IsRepetition() const {
    auto node_state = node_history_.State(n_.state()->board_key(), this->OrHand());
    return node_state == NodeHistory::NodeState::kRepetitionOrInferior;
  }

  bool IsRepetitionAfter(Move move) const {
    Hand hand = or_color_ == n_.side_to_move() ? AfterHand(n_, move, this->OrHand()) : this->OrHand();
    auto node_state = node_history_.State(n_.board_key_after(move), hand);
    return node_state == NodeHistory::NodeState::kRepetitionOrInferior;
  }

  bool IsExceedLimit(Depth max_depth) const { return depth_ >= max_depth; }

  auto& Pos() { return n_; }
  auto& Pos() const { return n_; }
  Depth GetDepth() const { return depth_; }
  Key GetPathKey() const { return path_key_; }
  Key PathKeyAfter(Move m) const { return ::komori::PathKeyAfter(path_key_, m, depth_); }
  std::uint64_t GetMoveCount() const { return move_count_; }

 private:
  Position& n_;
  Color or_color_;
  Depth depth_{};
  NodeHistory node_history_{};
  std::stack<StateInfo> st_info_{};
  Key path_key_{};
  std::uint64_t move_count_{};
};
}  // namespace komori
#endif  // NODE_HPP_