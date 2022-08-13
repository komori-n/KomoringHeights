#ifndef KOMORI_NEW_CC_HPP_
#define KOMORI_NEW_CC_HPP_

#include <algorithm>

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
  constexpr std::uint32_t& front() { return data_[0]; }
  constexpr const std::uint32_t& front() const { return data_[0]; }

 private:
  std::array<std::uint32_t, kMaxCheckMovesPerNode> data_;
  std::uint32_t len_{0};
};

class DelayedMoves {
 public:
  explicit DelayedMoves(const Node& n) : n_{n} {}

  std::optional<std::size_t> Add(Move move, std::size_t i_raw) {
    if (!IsDelayable(move)) {
      return std::nullopt;
    }

    for (std::size_t i = 0; i < len_; ++i) {
      const auto m = moves_[i].first;
      if (IsSame(m, move)) {
        const auto ret = moves_[i].second;
        moves_[i] = {move, i_raw};
        return ret;
      }
    }

    if (len_ < kMaxLen) {
      moves_[len_++] = {move, i_raw};
    }
    return std::nullopt;
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
        const auto l_is_rep = left_result.final_data.is_repetition;
        const auto r_is_rep = right_result.final_data.is_repetition;

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
      const auto i_raw = next_i_raw++;
      const auto hand_after = n.OrHandAfter(move.move);
      idx_.Push(i_raw);
      auto& child = children_[i_raw];
      auto& result = results_[i_raw];
      auto& query = queries_[i_raw];

      if (n.IsRepetitionOrInferiorAfter(move.move)) {
        if (!found_rep) {
          found_rep = true;
          result.InitFinal<false, true>(hand_after, len, 1);
        } else {
          idx_.Pop();
          continue;
        }
      } else {
        child = {0};
        query = tt.BuildChildQuery(n, move.move);
        result = query.LookUp(does_have_old_child_, len, false, [&n, &move]() { return InitialPnDn(n, move.move); });

        if (!result.IsFinal() && !or_node_ && first_search && result.unknown_data.is_first_visit) {
          nn.DoMove(move.move);
          if (!detail::DoesHaveMatePossibility(n.Pos())) {
            const auto hand = HandSet{DisproofHandTag{}}.Get(n.Pos());
            const auto len = MateLen{0, static_cast<std::uint16_t>(CountHand(n.OrHand()))};
            result.InitFinal<false>(hand, len, 1);
            query.SetResult(result);
          } else if (auto [best_move, proof_hand] = detail::CheckMate1Ply(nn); proof_hand != kNullHand) {
            const auto len = MateLen{1, static_cast<std::uint16_t>(CountHand(proof_hand))};
            result.InitFinal<true>(proof_hand, len, 1);
            query.SetResult(result);
          }
          nn.UndoMove(move.move);
        }

        if (!result.IsFinal()) {
          if (const auto res = delayed_moves.Add(move.move, i_raw)) {
            auto& child_dep = children_[*res];
            child_dep.next_dep = i_raw + 1;
            idx_.Pop();
          }
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
  bool BestMoveIsFirstVisit() const { return FrontResult().unknown_data.is_first_visit; }
  BitSet64 BestMoveSumMask() const { return BitSet64{~FrontResult().unknown_data.secret}; }

  tt::SearchResult CurrentResult(const Node& n) const {
    if (GetPn() == 0) {
      return GetProvenResult(n);
    } else if (GetDn() == 0) {
      return GetDisprovenResult(n);
    } else {
      return GetUnknownResult();
    }
  }

 private:
  const tt::SearchResult& FrontResult() const { return results_[idx_.front()]; }

  // <PnDn>
  constexpr PnDn GetPn() const {
    if (or_node_) {
      return GetPhi();
    } else {
      return GetDelta();
    }
  }
  constexpr PnDn GetDn() const {
    if (or_node_) {
      return GetDelta();
    } else {
      return GetPhi();
    }
  }

  constexpr PnDn GetPhi() const {
    if (idx_.empty()) {
      return kInfinitePnDn;
    }
    return FrontResult().Phi(or_node_);
  }

  constexpr PnDn GetDelta() const {
    auto [sum_delta, max_delta] = GetRawDelta();
    if (sum_delta == 0 && max_delta == 0) {
      return 0;
    }

    // 後回しにしている子局面が存在する場合、その値をδ値に加算しないと局面を過大評価してしまう。
    //
    // 例） sfen +P5l2/4+S4/p1p+bpp1kp/6pgP/3n1n3/P2NP4/3P1NP2/2P2S3/3K3L1 b RGSL2Prb2gsl3p 159
    //      1筋の合駒を考える時、玉方が合駒を微妙に変えることで読みの深さを指数関数的に大きくできてしまう
    if (mp_.size() > idx_.size()) {
      // 読みの後回しが原因の（半）無限ループを回避できればいいので、1点減点しておく
      sum_delta += 1;
    }

    return sum_delta + max_delta;
  }

  constexpr std::pair<PnDn, PnDn> GetRawDelta() const {
    if (idx_.empty()) {
      return {0, 0};
    }

    const auto& best_result = FrontResult();
    // 差分計算用の値を予め持っているので、高速に計算できる
    auto sum_delta = sum_delta_except_best_;
    auto max_delta = max_delta_except_best_;
    if (sum_mask_[idx_.front()]) {
      sum_delta += best_result.Delta(or_node_);
    } else {
      max_delta = std::max(max_delta, best_result.Delta(or_node_));
    }

    return {sum_delta, max_delta};
  }

  constexpr PnDn GetSecondPhi() const {
    if (idx_.size() <= 1) {
      return kInfinitePnDn;
    }
    const auto& second_best_result = results_[idx_[1]];
    return second_best_result.Phi(or_node_);
  }
  // </PnDn>

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

  tt::SearchResult GetProvenResult(const Node& n) const {
    if (or_node_) {
      const auto& result = FrontResult();
      const auto best_move = mp_[idx_[0]];
      const auto proof_hand = BeforeHand(n.Pos(), best_move, result.hand);
      const auto mate_len = result.len;
      const auto amount = result.amount;

      return {0, kInfinitePnDn, proof_hand, mate_len, amount, tt::UnknownData{false}};
    } else {
      // 子局面の証明駒の極小集合を計算する
      HandSet set{ProofHandTag{}};
      MateLen mate_len = kZeroMateLen;
      std::uint32_t amount = 1;
      for (const auto i_raw : idx_) {
        const auto& result = results_[i_raw];

        set.Update(result.hand);
        amount = std::max(amount, result.amount);
        if (result.len > mate_len) {
          mate_len = result.len + 1;
        }
      }
      const auto proof_hand = set.Get(n.Pos());

      // amount の総和を取ると値が大きくなりすぎるので子の数だけ足す
      amount += std::max(mp_.size(), std::size_t{1}) - 1;

      return {0, kInfinitePnDn, proof_hand, mate_len, amount, tt::UnknownData{false}};
    }
  }

  tt::SearchResult GetDisprovenResult(const Node& n) const {
    // children_ は千日手エントリが手前に来るようにソートされているので、以下のようにして千日手判定ができる
    if (!mp_.empty()) {
      if (const auto& result = FrontResult(); result.dn == 0 && result.final_data.is_repetition) {
        return {kInfinitePnDn, 0, n.OrHand(), {0, 0}, 1, tt::FinalData{false}};
      }
    }

    // フツーの不詰
    if (or_node_) {
      // 子局面の反証駒の極大集合を計算する
      HandSet set{DisproofHandTag{}};
      MateLen mate_len = kMaxMateLen;
      std::uint32_t amount = 1;
      for (const auto i_raw : idx_) {
        const auto& result = results_[i_raw];
        const auto child_move = mp_[i_raw];

        set.Update(BeforeHand(n.Pos(), child_move, result.hand));
        amount = std::max(amount, result.amount);
        if (result.len > mate_len) {
          mate_len = result.len + 1;
        }
      }
      amount += std::max(mp_.size(), std::size_t{1}) - 1;
      const auto disproof_hand = set.Get(n.Pos());

      return {kInfinitePnDn, 0, disproof_hand, mate_len, amount, tt::FinalData{false}};
    } else {
      const auto& result = FrontResult();
      auto disproof_hand = result.hand;
      const auto mate_len = result.len + 1;
      const auto amount = result.amount;

      // 駒打ちならその駒を持っていないといけない
      if (const auto best_move = mp_[idx_[0]]; is_drop(best_move)) {
        const auto pr = move_dropped_piece(best_move);
        const auto pr_cnt = hand_count(MergeHand(n.OrHand(), n.AndHand()), pr);
        const auto disproof_pr_cnt = hand_count(disproof_hand, pr);
        if (pr_cnt - disproof_pr_cnt <= 0) {
          // もし現局面の攻め方の持ち駒が disproof_hand だった場合、打とうとしている駒 pr が攻め方に独占されているため
          // 受け方は BestMove() を着手することができない。そのため、攻め方の持ち駒を何枚か受け方に渡す必要がある。
          sub_hand(disproof_hand, pr, disproof_pr_cnt);
          add_hand(disproof_hand, pr, pr_cnt - 1);
        }
      }

      return {kInfinitePnDn, 0, disproof_hand, mate_len, amount, tt::FinalData{false}};
    }
  }

  tt::SearchResult GetUnknownResult() const {
    const auto& result = FrontResult();
    std::uint32_t amount = result.amount + mp_.size() / 2;

    Key parent_board_key{kNullKey};
    Hand parent_hand{kNullHand};
    if (parent_ != nullptr) {
      parent_board_key = parent_->board_key_;
      parent_hand = parent_->or_hand_;
    }

    tt::UnknownData unknown_data{false, parent_board_key, parent_hand, ~sum_mask_.Value()};
    return {GetPn(), GetDn(), or_hand_, len_, amount, unknown_data};
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