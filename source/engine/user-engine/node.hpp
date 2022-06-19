#ifndef NODE_HPP_
#define NODE_HPP_

#include <stack>
#include <vector>

#include "hands.hpp"
#include "node_history.hpp"
#include "path_keys.hpp"
#include "typedefs.hpp"

namespace komori {
/**
 * @brief Position を wrap して千日手判定や OR/AND node の区別など詰将棋探索に必要な機能を追加したクラス
 */
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
  }

  void UndoMove(Move m) {
    depth_--;
    n_.undo_move(m);
    st_info_.pop();
    node_history_.Leave(n_.state()->board_key(), this->OrHand());
    path_key_ = PathKeyBefore(path_key_, m, depth_);
  }

  Square KingSquare() const {
    const auto side_to_move = n_.side_to_move();
    const auto or_node = IsOrNode();
    const auto king_side = or_node ? ~side_to_move : side_to_move;

    return n_.king_square(king_side);
  }

  void StealCapturedPiece() {
    auto captured_pr = raw_type_of(n_.state()->capturedPiece);
    path_key_ = PathKeyAfterSteal(path_key_, captured_pr, depth_);
    n_.steal_hand(captured_pr);
  }

  void UnstealCapturedPiece() {
    auto captured_pr = raw_type_of(n_.state()->capturedPiece);
    path_key_ = PathKeyAfterGive(path_key_, captured_pr, depth_);
    n_.give_hand(captured_pr);
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

  Move ImmediateCapture() const {
    if (!IsOrNode() || GetDepth() < 1) {
      return MOVE_NONE;
    }

    // 両王手のときは少しだけ注意が必要。以下のコードで問題ない
    // - 近接王手駒がchecker_sq: between_bb の結果が空なので次の if 文の中に入る
    // - 遠隔王手駒がchecker_sq: between_bb へ駒を打ったり移動したりする手は王手回避にならない。
    //   つまり、必ず次の if 文の中に入る

    auto last_move = n_.state()->lastMove;
    auto checker_sq = n_.state()->previous->checkersBB.pop_c();
    auto king_sq = n_.king_square(~n_.side_to_move());

    auto between = between_bb(king_sq, checker_sq);
    if (!between.test(to_sq(last_move))) {
      return MOVE_NONE;
    }

    auto checker = n_.piece_on(checker_sq);
    auto capture_move = make_move(checker_sq, to_sq(last_move), checker);
    if (n_.pseudo_legal(capture_move) && n_.legal(capture_move)) {
      return capture_move;
    }
    return MOVE_NONE;
  }

  bool IsRepetition() const { return node_history_.Contains(n_.state()->board_key(), this->OrHand()); }

  bool IsRepetitionAfter(Move move) const {
    return node_history_.Contains(n_.board_key_after(move), this->OrHandAfter(move));
  }

  bool IsRepetitionOrInferior() const { return node_history_.IsInferior(n_.state()->board_key(), this->OrHand()); }

  bool IsRepetitionOrInferiorAfter(Move move) const {
    return node_history_.IsInferior(n_.board_key_after(move), this->OrHandAfter(move));
  }

  bool IsRepetitionOrSuperior() const { return node_history_.IsSuperior(n_.state()->board_key(), this->OrHand()); }

  bool IsRepetitionOrSuperiorAfter(Move move) const {
    return node_history_.IsSuperior(n_.board_key_after(move), this->OrHandAfter(move));
  }

  bool ContainsInPath(Key key, Hand hand) const {
    return node_history_.Contains(key, hand) || (n_.state()->board_key() == key && OrHand() == hand);
  }

  bool ContainsInPath(Key key) const { return node_history_.Contains(key) || n_.state()->board_key() == key; }

  bool IsExceedLimit(Depth max_depth) const { return depth_ >= max_depth; }

  auto& Pos() { return n_; }
  auto& Pos() const { return n_; }
  Depth GetDepth() const { return depth_; }
  Key GetPathKey() const { return path_key_; }
  Key PathKeyAfter(Move m) const { return ::komori::PathKeyAfter(path_key_, m, depth_); }
  bool IsOrNode() const { return n_.side_to_move() == or_color_; }

  Color OrColor() const { return or_color_; }
  Color AndColor() const { return ~or_color_; }

 private:
  Position& n_;                      ///< 現在の局面
  Color or_color_;                   ///< OR node（攻め方）の手番
  Depth depth_{};                    ///< root から数えた探索深さ
  NodeHistory node_history_{};       ///< 千日手・優等局面の一覧
  std::stack<StateInfo> st_info_{};  ///< do_move で必要な一時領域
  Key path_key_{};                   ///< 経路ハッシュ値。差分計算により求める。
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
