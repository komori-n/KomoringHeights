#ifndef NODE_HPP_
#define NODE_HPP_

#include <functional>
#include <stack>
#include <vector>

#include "hands.hpp"
#include "node_history.hpp"
#include "path_keys.hpp"
#include "typedefs.hpp"

namespace komori {
/**
 * @brief 局面や探索深さ、経路ハッシュ（path_key）などを保持するクラス
 */
class Node {
 public:
  /**
   * @brief コンストラクタ
   *
   * @param n         開始局面
   * @param or_node   開始局面が OR Node かどうか
   * @param path_key  開始局面の path key。この値を基準に子局面の path key を差分計算する
   * @param depth     開始局面の探索深さ
   */
  explicit Node(Position& n, bool or_node, Key path_key = 0, Depth depth = 0)
      : n_{std::ref(n)},
        or_color_{or_node ? n.side_to_move() : ~n.side_to_move()},
        depth_{depth},
        path_key_{path_key} {}
  Node() = delete;
  Node(const Node&) = delete;
  Node(Node&&) noexcept = default;
  Node& operator=(const Node&) = delete;
  Node& operator=(Node&&) noexcept = default;
  ~Node() = default;

  Position& Pos() { return n_.get(); }
  Position& Pos() const { return n_.get(); }
  explicit operator Position&() { return Pos(); }
  explicit operator const Position&() const { return Pos(); }

  Color Us() const { return Pos().side_to_move(); }
  Key GetKey() const { return Pos().key(); }
  Key BoardKey() const { return Pos().state()->board_key(); }
  Key BoardKeyAfter(Move m) const { return Pos().board_key_after(m); }

  void DoMove(Move m) {
    path_key_ = PathKeyAfter(m);
    node_history_.Visit(BoardKey(), this->OrHand());
    Pos().do_move(m, st_info_.emplace());
    depth_++;
  }

  void UndoMove(Move m) {
    depth_--;
    Pos().undo_move(m);
    st_info_.pop();
    node_history_.Leave(BoardKey(), this->OrHand());
    path_key_ = PathKeyBefore(m);
  }

  void StealCapturedPiece() {
    auto captured_pr = raw_type_of(Pos().state()->capturedPiece);
    path_key_ = PathKeyAfterSteal(path_key_, captured_pr, depth_);
    Pos().steal_hand(captured_pr);
  }

  void UnstealCapturedPiece() {
    auto captured_pr = raw_type_of(Pos().state()->capturedPiece);
    path_key_ = PathKeyAfterGive(path_key_, captured_pr, depth_);
    Pos().give_hand(captured_pr);
  }

  Hand OrHand() const { return Pos().hand_of(or_color_); }
  Hand AndHand() const { return Pos().hand_of(~or_color_); }
  Hand OrHandAfter(Move move) const {
    if (IsOrNode()) {
      return AfterHand(Pos(), move, OrHand());
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

    auto last_move = Pos().state()->lastMove;
    auto checker_sq = Pos().state()->previous->checkersBB.pop_c();
    auto king_sq = Pos().king_square(~Pos().side_to_move());

    auto between = between_bb(king_sq, checker_sq);
    if (!between.test(to_sq(last_move))) {
      return MOVE_NONE;
    }

    auto checker = Pos().piece_on(checker_sq);
    auto capture_move = make_move(checker_sq, to_sq(last_move), checker);
    if (Pos().pseudo_legal(capture_move) && Pos().legal(capture_move)) {
      return capture_move;
    }
    return MOVE_NONE;
  }

  bool IsRepetition() const { return node_history_.Contains(BoardKey(), this->OrHand()); }

  bool IsRepetitionAfter(Move move) const {
    return node_history_.Contains(BoardKeyAfter(move), this->OrHandAfter(move));
  }

  bool IsRepetitionOrInferior() const { return node_history_.IsInferior(BoardKey(), this->OrHand()); }

  bool IsRepetitionOrInferiorAfter(Move move) const {
    return node_history_.IsInferior(BoardKeyAfter(move), this->OrHandAfter(move));
  }

  bool IsRepetitionOrSuperior() const { return node_history_.IsSuperior(BoardKey(), this->OrHand()); }

  bool IsRepetitionOrSuperiorAfter(Move move) const {
    return node_history_.IsSuperior(BoardKeyAfter(move), this->OrHandAfter(move));
  }

  bool ContainsInPath(Key key, Hand hand) const {
    return node_history_.Contains(key, hand) || (BoardKey() == key && OrHand() == hand);
  }

  bool ContainsInPath(Key key) const { return node_history_.Contains(key) || BoardKey() == key; }
  Depth GetDepth() const { return depth_; }
  Key GetPathKey() const { return path_key_; }
  Key PathKeyAfter(Move m) const { return ::komori::PathKeyAfter(path_key_, m, depth_); }
  bool IsOrNode() const { return n_.get().side_to_move() == or_color_; }

  Color OrColor() const { return or_color_; }
  Color AndColor() const { return ~or_color_; }

 private:
  Key PathKeyBefore(Move m) const { return ::komori::PathKeyBefore(path_key_, m, depth_); }

  std::reference_wrapper<Position> n_;  ///< 現在の局面
  Color or_color_;                      ///< OR node（攻め方）の手番
  Depth depth_{};                       ///< root から数えた探索深さ
  NodeHistory node_history_{};          ///< 千日手・優等局面の一覧
  std::stack<StateInfo> st_info_{};     ///< do_move で必要な一時領域
  Key path_key_{};                      ///< 経路ハッシュ値。差分計算により求める。
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
