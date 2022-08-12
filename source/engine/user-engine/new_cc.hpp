#ifndef KOMORI_NEW_CC_HPP_
#define KOMORI_NEW_CC_HPP_

#include "../../mate/mate.h"
#include "bitset.hpp"
#include "hands.hpp"
#include "initial_estimation.hpp"
#include "move_picker.hpp"
#include "new_ttentry.hpp"
#include "node.hpp"

namespace komori {
namespace detail {
struct Child {
  std::uint32_t next_dep;
};

class IndexTable {
 public:
  constexpr std::uint32_t Push(std::uint32_t i_raw) {
    const auto i = len_++;
    data_[i] = i_raw;
    return i;
  }
  constexpr void Pop() { --len_; }

  constexpr std::uint32_t operator[](std::uint32_t i) const { return data_[i]; }

  constexpr auto begin() { return data_.begin(); }
  constexpr auto begin() const { return data_.begin(); }
  constexpr auto end() { return data_.begin() + len_; }
  constexpr auto end() const { return data_.begin() + len_; }
  constexpr auto size() const { return len_; }
  constexpr bool empty() const { return len_ == 0; }
  constexpr std::uint32_t& front() {
    KOMORI_PRECONDITION(!empty());
    return data_[0];
  }
  constexpr const std::uint32_t& front() const {
    KOMORI_PRECONDITION(!empty());
    return data_[0];
  }

 private:
  std::array<std::uint32_t, kMaxCheckMovesPerNode> data_;
  std::uint32_t len_{0};
};

class DelayedMoves {
 public:
  explicit DelayedMoves(const Node& n) : n_{n} {}

  std::optional<std::size_t> Get(Move move) {
    if (!IsDelayable(move)) {
      return std::nullopt;
    }

    for (std::size_t i = 0; i < len_; ++i) {
      const auto m = moves_[i].first;
      if (IsSame(m, move)) {
        return moves_[i].second;
      }
    }
    return std::nullopt;
  }

  void Add(Move move, std::size_t i_raw) {
    if (!IsDelayable(move)) {
      return;
    }

    for (std::size_t i = 0; i < len_; ++i) {
      const auto m = moves_[i].first;
      if (IsSame(m, move)) {
        moves_[i] = {move, i_raw};
        return;
      }
    }

    if (len_ < kMaxLen) {
      moves_[len_++] = {move, i_raw};
    }
  }

 private:
  static constexpr inline std::size_t kMaxLen = 10;

  bool IsDelayable(Move move) const {
    const Color us = n_.Pos().side_to_move();
    const auto to = to_sq(move);

    if (is_drop(move)) {
      if (n_.IsOrNode()) {
        return false;
      } else {
        return true;
      }
    } else {
      const Square from = from_sq(move);
      const Piece moved_piece = n_.Pos().piece_on(from);
      const PieceType moved_pr = type_of(moved_piece);
      if (enemy_field(us).test(from) || enemy_field(us).test(to)) {
        if (moved_pr == PAWN || moved_pr == BISHOP || moved_pr == ROOK) {
          return true;
        }

        if (moved_pr == LANCE) {
          if (us == BLACK) {
            return rank_of(to) == RANK_2;
          } else {
            return rank_of(to) == RANK_8;
          }
        }
      }
    }

    return false;
  }

  bool IsSame(Move m1, Move m2) const {
    const auto to1 = to_sq(m1);
    const auto to2 = to_sq(m2);
    if (is_drop(m1) && is_drop(m2)) {
      return to1 == to2;
    } else if (!is_drop(m1) && !is_drop(m2)) {
      const auto from1 = from_sq(m1);
      const auto from2 = from_sq(m2);
      return from1 == from2 && to1 == to2;
    } else {
      return false;
    }
  }

  const Node& n_;
  std::array<std::pair<Move, std::size_t>, kMaxLen> moves_;
  std::size_t len_{0};
};

inline bool DoesHaveMatePossibility(const Position& n) {
  auto us = n.side_to_move();
  auto them = ~us;
  auto hand = n.hand_of(us);
  auto king_sq = n.king_square(them);

  auto droppable_bb = ~n.pieces();
  for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
    if (hand_exists(hand, pr)) {
      if (pr == PAWN && (n.pieces(us, PAWN) & file_bb(file_of(king_sq)))) {
        continue;
      }

      if (droppable_bb.test(StepEffect(pr, them, king_sq))) {
        return true;
      }
    }
  }

  auto x = ((n.pieces(PAWN) & check_candidate_bb(us, PAWN, king_sq)) |
            (n.pieces(LANCE) & check_candidate_bb(us, LANCE, king_sq)) |
            (n.pieces(KNIGHT) & check_candidate_bb(us, KNIGHT, king_sq)) |
            (n.pieces(SILVER) & check_candidate_bb(us, SILVER, king_sq)) |
            (n.pieces(GOLDS) & check_candidate_bb(us, GOLD, king_sq)) |
            (n.pieces(BISHOP) & check_candidate_bb(us, BISHOP, king_sq)) | (n.pieces(ROOK_DRAGON)) |
            (n.pieces(HORSE) & check_candidate_bb(us, ROOK, king_sq))) &
           n.pieces(us);
  auto y = n.blockers_for_king(them) & n.pieces(us);

  return x | y;
}

inline std::pair<Move, Hand> CheckMate1Ply(Node& n) {
  if (!n.Pos().in_check()) {
    if (auto move = Mate::mate_1ply(n.Pos()); move != MOVE_NONE) {
      n.DoMove(move);
      auto hand = HandSet{ProofHandTag{}}.Get(n.Pos());
      n.UndoMove(move);

      return {move, BeforeHand(n.Pos(), move, hand)};
    }
  }
  return {MOVE_NONE, kNullHand};
}
}  // namespace detail

class ChildrenCache {
 private:
  auto MakeComparer() const {
    return [this](std::size_t i_raw, std::size_t j_raw) -> bool {
      const auto& left_result = results_[i_raw];
      const auto& right_result = results_[j_raw];

      if (left_result.Phi(or_node_) != right_result.Phi(or_node_)) {
        return left_result.Phi(or_node_) < right_result.Phi(or_node_);
      } else if (left_result.Delta(or_node_) != right_result.Delta(or_node_)) {
        return left_result.Delta(or_node_) > right_result.Delta(or_node_);
      }

      if (left_result.dn == 0 && right_result.dn == 0) {
        const auto l_is_rep = left_result.is_repetition;
        const auto r_is_rep = right_result.is_repetition;

        if (l_is_rep != r_is_rep) {
          return !or_node_ ^ (static_cast<int>(l_is_rep) < static_cast<int>(r_is_rep));
        }
      }

      return mp_[i_raw] < mp_[j_raw];
    };
  }

 public:
  ChildrenCache(tt::TranspositionTable& tt,
                const Node& n,
                MateLen len,
                bool first_search,
                BitSet64 sum_mask = BitSet64::Full(),
                ChildrenCache* parent = nullptr)
      : or_node_{n.IsOrNode()},
        mp_{n, true},
        len_{len},
        sum_mask_{sum_mask},
        parent_{parent},
        board_key_{n.Pos().state()->board_key()},
        or_hand_{n.OrHand()} {
    bool found_rep = false;
    Node& nn = const_cast<Node&>(n);

    detail::DelayedMoves delayed_moves{nn};
    std::uint32_t next_i_raw = 0;
    for (const auto& move : mp_) {
      const auto hand_after = n.OrHandAfter(move.move);
      const auto i_raw = next_i_raw++;
      idx_.Push(i_raw);
      auto& child = children_[i_raw];
      auto& result = results_[i_raw];
      auto& query = queries_[i_raw];

      if (n.IsRepetitionOrInferiorAfter(move.move)) {
        if (!found_rep) {
          found_rep = true;
          result.pn = kInfinitePnDn;
          result.dn = 0;
          result.hand = hand_after;
          result.len = len;
          result.is_repetition = true;
        } else {
          idx_.Pop();
          continue;
        }
      } else {
        child = {0};
        query = tt.BuildChildQuery(n, move.move);
        result = query.LookUp(len, false, [&n, &move]() { return InitialPnDn(n, move.move); });
        if (result.IsOldChild(n.GetDepth())) {
          does_have_old_child_ = true;
        }

        if (!or_node_ && first_search && result.is_first_visit) {
          nn.DoMove(move.move);
          if (!detail::DoesHaveMatePossibility(n.Pos())) {
            result.pn = kInfinitePnDn;
            result.dn = 0;
            result.hand = HandSet{DisproofHandTag{}}.Get(n.Pos());
            result.len = {0, static_cast<std::uint16_t>(CountHand(n.OrHand()))};
            query.SetResult(result);
          } else if (auto [best_move, proof_hand] = detail::CheckMate1Ply(nn); proof_hand != kNullHand) {
            result.pn = 0;
            result.dn = kInfinitePnDn;
            result.hand = proof_hand;
            result.len = {1, static_cast<std::uint16_t>(CountHand(proof_hand))};
            query.SetResult(result);
          }
          nn.UndoMove(move.move);
        }

        if (!result.IsFinal()) {
          if (const auto res = delayed_moves.Get(move.move)) {
            auto& child_dep = children_[*res];
            child_dep.next_dep = i_raw + 1;
            idx_.Pop();
          }
          delayed_moves.Add(move.move, i_raw);
        }
      }

      if (result.Phi(or_node_) == 0) {
        break;
      }
    }

    std::sort(idx_.begin(), idx_.end(), MakeComparer());
    RecalcDelta();

    if (!idx_.empty()) {
      EliminateDoubleCount(tt, n, 0);
    }
  }

  ChildrenCache(const ChildrenCache&) = delete;
  ChildrenCache(ChildrenCache&&) = delete;
  ChildrenCache& operator=(const ChildrenCache&) = delete;
  ChildrenCache& operator=(ChildrenCache&&) = delete;
  ~ChildrenCache() = default;

  Move BestMove() const { return mp_[idx_.front()].move; };
  bool BestMoveIsFirstVisit() const { return FrontResult().is_first_visit; }
  BitSet64 BestMoveSumMask() const { return BitSet64{~FrontResult().secret}; }

  tt::SearchResult CurrentResult(const Node& n) const {
    // if (GetPn() == 0) {
    //   return GetProvenResult(n);
    // } else if (GetDn() == 0) {
    //   return GetDisprovenResult(n);
    // } else {
    //   return GetUnknownResult(n);
    // }
  }

  PnDn GetPn() const { return 1; }
  PnDn GetDn() const { return 1; }

 private:
  const tt::SearchResult& FrontResult() const { return results_[idx_.front()]; }

  constexpr void RecalcDelta() {
    sum_delta_except_best_ = 0;
    max_delta_except_best_ = 0;

    for (decltype(idx_.size()) i = 1; i < idx_.size(); ++i) {
      const auto i_raw = idx_[i];
      const auto& result = results_[i_raw];
      if (sum_mask_[i_raw]) {
        sum_delta_except_best_ += result.Delta(or_node_);
      } else {
        max_delta_except_best_ = std::max(max_delta_except_best_, result.Delta(or_node_));
      }
    }
  }

  tt::SearchResult GetUnknownResult(const Node& n) const {
    const auto& result = FrontResult();
    std::uint32_t amount = result.amount + mp_.size() / 2;
    tt::SearchResult ret{GetPn(), GetDn(), or_hand_, len_};
    ret.is_first_visit = false;
    ret.amount = amount;
    ret.min_depth = n.GetDepth();
    ret.secret = ~sum_mask_.Value();
    if (parent_ != nullptr) {
      ret.parent_board_key = parent_->board_key_;
      ret.parent_hand = parent_->or_hand_;
    } else {
      ret.parent_board_key = kNullKey;
      ret.parent_hand = kNullHand;
    }
    return ret;
  }

  void EliminateDoubleCount(tt::TranspositionTable& tt, const Node& n, std::size_t i) {
    // unimplemented
  }

  const bool or_node_;
  const MovePicker mp_;
  const MateLen len_;

  ChildrenCache* const parent_;
  const Key board_key_;
  const Hand or_hand_;

  std::array<detail::Child, kMaxCheckMovesPerNode> children_;
  std::array<tt::SearchResult, kMaxCheckMovesPerNode> results_;
  std::array<tt::Query, kMaxCheckMovesPerNode> queries_;

  bool does_have_old_child_{false};

  PnDn sum_delta_except_best_;
  PnDn max_delta_except_best_;

  BitSet64 sum_mask_;
  detail::IndexTable idx_;
};
}  // namespace komori

#endif  // KOMORI_NEW_CC_HPP_