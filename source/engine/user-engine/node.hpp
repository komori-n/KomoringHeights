/**
 * @file node.hpp
 */
#ifndef KOMORI_NODE_HPP_
#define KOMORI_NODE_HPP_

#include <functional>
#include <stack>
#include <utility>
#include <vector>

#include "../../mate/mate.h"
#include "hands.hpp"
#include "path_keys.hpp"
#include "typedefs.hpp"
#include "visit_history.hpp"

namespace komori {
/**
 * @brief 局面や探索深さ、経路ハッシュ（path_key）などを保持するクラス
 *
 * 詰将棋探索では、`Position` とセットで以下の情報がほしいことがある。
 *
 * - OR node / AND node
 * - 経路ハッシュ値
 * - 探索深さ
 * - 千日手、優等ループ、劣等ループ
 *
 * このクラスでは、`Position` に加えて上記の情報を提供する。
 *
 * @note このクラスでは、`Position` を参照として保持する。よって、クラス外で `Position` を書き換えると
 * `Node` の内部状態も狂ってしまうので注意すること。
 */
class Node {
 public:
  /**
   * @brief コンストラクタ
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
  /// Default constructor(delete)
  Node() = delete;
  /// Copy constructor(delete)
  Node(const Node&) = delete;
  /// Move constructor(default)
  Node(Node&&) noexcept = default;
  /// Copy assign operator(delete)
  Node& operator=(const Node&) = delete;
  /// Move assign operator(delete)
  Node& operator=(Node&&) noexcept = default;
  /// Destructor(default)
  ~Node() = default;

  /// このクラスが保持する `Position` への参照
  Position& Pos() { return n_.get(); }
  /// このクラスが保持する `Position` への参照
  const Position& Pos() const { return n_.get(); }

  /// 現在の手番
  Color Us() const { return Pos().side_to_move(); }
  /// OR node（攻め方）の手番
  Color OrColor() const { return or_color_; }
  /// AND node（玉方）の手番
  Color AndColor() const { return ~or_color_; }
  /// 現在 OR node かどうか
  bool IsOrNode() const { return Us() == or_color_; }
  /// 現在の攻め方の持ち駒
  Hand OrHand() const { return Pos().hand_of(or_color_); }
  /// 現在の玉方の持ち駒
  Hand AndHand() const { return Pos().hand_of(~or_color_); }
  /// 開始局面が OR Node かどうか
  bool IsRootOrNode() const { return IsOrNode() ^ (GetDepth() % 2 == 1); }

  /// 現在の探索深さ
  Depth GetDepth() const { return depth_; }

  /// 現在のハッシュ値
  Key GetKey() const { return Pos().key(); }
  /// 現在の盤面ハッシュ値
  Key BoardKey() const { return Pos().state()->board_key(); }
  /// 現在の経路ハッシュ値
  Key GetPathKey() const { return path_key_; }

  /// `move` 後のハッシュ値
  Key KeyAfter(Move move) const { return Pos().key_after(move); }
  /// `move` 後の盤面ハッシュ値
  Key BoardKeyAfter(Move move) const { return Pos().board_key_after(move); }
  /// `move` 後の経路ハッシュ値
  Key PathKeyAfter(Move move) const { return ::komori::PathKeyAfter(path_key_, move, depth_); }
  /// `move` 後の攻め方の持ち駒
  Hand OrHandAfter(Move move) const {
    if (IsOrNode()) {
      return AfterHand(Pos(), move, OrHand());
    } else {
      return OrHand();
    }
  }

  /// `move` で1手進める
  void DoMove(Move move) {
    path_key_ = PathKeyAfter(move);
    visit_history_.Visit(BoardKey(), this->OrHand());
    Pos().do_move(move, st_info_.emplace());
    depth_++;
  }

  /// `DoMove()` で勧めた局面を元に戻す
  void UndoMove() {
    const auto last_move = Pos().state()->lastMove;

    depth_--;
    Pos().undo_move(last_move);
    st_info_.pop();
    visit_history_.Leave(BoardKey(), this->OrHand());
    path_key_ = PathKeyBefore(last_move);
  }

  /// (not maintained) 直前に相手が取った駒を奪う。`UnstealCapturedPiece()` をするまでは `UndoMove()` してはいけない。
  [[deprecated]] void StealCapturedPiece() {
    auto captured_pr = raw_type_of(Pos().state()->capturedPiece);
    path_key_ = PathKeyAfterSteal(path_key_, captured_pr, depth_);
    Pos().steal_hand(captured_pr);
  }

  /// (not maintained) `StealCapturedPiece()` で奪った駒を返す
  [[deprecated]] void UnstealCapturedPiece() {
    auto captured_pr = raw_type_of(Pos().state()->capturedPiece);
    path_key_ = PathKeyAfterGive(path_key_, captured_pr, depth_);
    Pos().give_hand(captured_pr);
  }

  /// (not maintained) 直前の王手駒をすぐに取り返す手を生成する。もしできなければ `MOVE_NONE` を返す。
  [[deprecated]] Move ImmediateCapture() const {
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

  /// 現局面が千日手かどうか
  bool IsRepetition() const { return visit_history_.Contains(BoardKey(), this->OrHand()); }

  /// `move` をすると千日手になるかどうか
  bool IsRepetitionAfter(Move move) const {
    return visit_history_.Contains(BoardKeyAfter(move), this->OrHandAfter(move));
  }

  /// 現局面が千日手または劣等局面か
  bool IsRepetitionOrInferior() const { return visit_history_.IsInferior(BoardKey(), this->OrHand()); }

  /// `move` をすると千日手または劣等局面になるかどうか
  bool IsRepetitionOrInferiorAfter(Move move) const {
    return visit_history_.IsInferior(BoardKeyAfter(move), this->OrHandAfter(move));
  }

  /// 現局面が千日手または優等局面か
  bool IsRepetitionOrSuperior() const { return visit_history_.IsSuperior(BoardKey(), this->OrHand()); }

  /// `move` をすると千日手または優等局面になるかどうか
  bool IsRepetitionOrSuperiorAfter(Move move) const {
    return visit_history_.IsSuperior(BoardKeyAfter(move), this->OrHandAfter(move));
  }

  /// (`board_key`, `hand`) が経路に含まれているかどうか
  bool ContainsInPath(Key board_key, Hand hand) const {
    return visit_history_.Contains(board_key, hand) || (BoardKey() == board_key && OrHand() == hand);
  }

  /// (`board_key`, *) が経路に含まれているかどうか
  bool ContainsInPath(Key board_key) const { return visit_history_.Contains(board_key) || BoardKey() == board_key; }

 private:
  /// `move` 直前の経路ハッシュ値
  Key PathKeyBefore(Move move) const { return ::komori::PathKeyBefore(path_key_, move, depth_); }

  /// 現在の局面。move construct 可能にするために生参照ではなく `std::reference_wrapper` で持つ。
  std::reference_wrapper<Position> n_;
  Color or_color_;                   ///< OR node（攻め方）の手番
  Depth depth_{};                    ///< root から数えた探索深さ
  VisitHistory visit_history_{};     ///< 千日手・優等局面の一覧
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
    n.UndoMove();
  }
}

/**
 * @brief (OR node限定) `n` が 1 手詰かどうか判定する。
 * @param n 現局面
 * @return `Move` 1手詰があればその手。なければ `MOVE_NONE`。
 * @return `Hand` 1手詰があればその証明駒。なければ `kNullHand`。
 * @note 攻め方の玉に王手がかかっている等、一部局面では1手詰が見つけられない事がある。
 */
inline std::pair<Move, Hand> CheckMate1Ply(Node& n) {
  if (!n.Pos().in_check()) {
    if (auto move = Mate::mate_1ply(n.Pos()); move != MOVE_NONE) {
      n.DoMove(move);
      auto hand = HandSet{ProofHandTag{}}.Get(n.Pos());
      n.UndoMove();

      return {move, BeforeHand(n.Pos(), move, hand)};
    }
  }
  return {MOVE_NONE, kNullHand};
}
}  // namespace komori
#endif  // KOMORI_NODE_HPP_
