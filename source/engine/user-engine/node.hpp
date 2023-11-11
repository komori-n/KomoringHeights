/**
 * @file node.hpp
 */
#ifndef KOMORI_NODE_HPP_
#define KOMORI_NODE_HPP_

#include <functional>
#include <utility>
#include <vector>

#include "../../mate/mate.h"
#include "board_key_hand_pair.hpp"
#include "fixed_size_stack.hpp"
#include "hands.hpp"
#include "path_keys.hpp"
#include "typedefs.hpp"
#include "visit_history.hpp"

namespace komori {
/**
 * @brief `Position` をラップして詰将棋特有の判定を追加するクラス。
 *
 * 詰将棋探索では、`Position` とセットで以下の情報をしばしば使用する。
 *
 * - OR node（攻方） / AND node（玉方）
 * - 攻方の持ち駒
 * - 経路ハッシュ値
 * - 探索深さ
 * - 千日手、優等ループ、劣等ループの高速な判定
 *   - `Position` でも可能だが、手数が長いと判定が遅い
 * - 1手後の XXX
 *   - XXX は持ち駒、ハッシュ値、千日手判定など
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

  /// 玉の位置
  Square KingSquare() const { return Pos().king_square(AndColor()); }

  /// 現在の探索深さ
  Depth GetDepth() const { return depth_; }

  /// 現在のハッシュ値
  Key GetKey() const { return Pos().key(); }
  /// 現在の盤面ハッシュ値
  Key BoardKey() const { return Pos().state()->board_key(); }
  /// 現在の経路ハッシュ値
  Key GetPathKey() const { return path_key_; }
  /// 盤面ハッシュ値と攻め方の持ち駒を同時に取得する
  BoardKeyHandPair GetBoardKeyHandPair() const { return {BoardKey(), OrHand()}; }

  /// 開始局面の指し手
  std::optional<Move> RootMove() const {
    if (moves_.empty()) {
      return std::nullopt;
    } else {
      return {moves_.front()};
    }
  }
  /// 開始局面からの指し手
  const FixedSizeStack<Move, kDepthMax>& MovesFromStart() const { return moves_; }

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
  /// `move` 後の盤面ハッシュ値と攻め方の持ち駒を同時に取得する
  BoardKeyHandPair BoardKeyHandPairAfter(Move move) const { return {BoardKeyAfter(move), OrHandAfter(move)}; }

  /// `move` で1手進める
  void DoMove(Move move) {
    moves_.Push(move);
    path_key_ = PathKeyAfter(move);
    visit_history_.Visit(BoardKey(), this->OrHand(), depth_);

    st_info_.Push(StateInfo{});
    Pos().do_move(move, st_info_.back());
    depth_++;
  }

  /// `DoMove()` で進めた局面を元に戻す
  void UndoMove() {
    const auto last_move = moves_.back();
    depth_--;
    Pos().undo_move(last_move);

    st_info_.Pop();
    visit_history_.Leave(BoardKey(), this->OrHand(), depth_);
    path_key_ = PathKeyBefore(last_move);
    moves_.Pop();
  }

  /// 現局面が千日手かどうか
  std::optional<Depth> IsRepetition() const { return visit_history_.Contains(BoardKey(), this->OrHand()); }

  /// `move` をすると千日手になるかどうか
  std::optional<Depth> IsRepetitionAfter(Move move) const {
    return visit_history_.Contains(BoardKeyAfter(move), this->OrHandAfter(move));
  }

  /// 現局面が千日手または劣等局面か
  std::optional<Depth> IsRepetitionOrInferior() const { return visit_history_.IsInferior(BoardKey(), this->OrHand()); }

  /// `move` をすると千日手または劣等局面になるかどうか
  std::optional<Depth> IsRepetitionOrInferiorAfter(Move move) const {
    return visit_history_.IsInferior(BoardKeyAfter(move), this->OrHandAfter(move));
  }

  /// 現局面が千日手または優等局面か
  std::optional<Depth> IsRepetitionOrSuperior() const { return visit_history_.IsSuperior(BoardKey(), this->OrHand()); }

  /// `move` をすると千日手または優等局面になるかどうか
  std::optional<Depth> IsRepetitionOrSuperiorAfter(Move move) const {
    return visit_history_.IsSuperior(BoardKeyAfter(move), this->OrHandAfter(move));
  }

  /// (`board_key`, `hand`) が経路に含まれているかどうか
  std::optional<Depth> ContainsInPath(Key board_key, Hand hand) const {
    if (const auto opt = visit_history_.Contains(board_key, hand)) {
      return opt;
    }

    if (BoardKey() == board_key && OrHand() == hand) {
      return depth_;
    }
    return std::nullopt;
  }

 private:
  /// `move` 直前の経路ハッシュ値
  Key PathKeyBefore(Move move) const { return ::komori::PathKeyBefore(path_key_, move, depth_); }

  /// 現在の局面。move construct 可能にするために生参照ではなく `std::reference_wrapper` で持つ。
  std::reference_wrapper<Position> n_;
  Color or_color_;                                  ///< OR node（攻め方）の手番
  Depth depth_{};                                   ///< root から数えた探索深さ
  VisitHistory visit_history_{};                    ///< 千日手・優等局面の一覧
  FixedSizeStack<Move, kDepthMax> moves_{};         ///< 開始局面からの指し手
  FixedSizeStack<StateInfo, kDepthMax> st_info_{};  ///< do_move で必要な一時領域
  Key path_key_{};                                  ///< 経路ハッシュ値。差分計算により求める。
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
