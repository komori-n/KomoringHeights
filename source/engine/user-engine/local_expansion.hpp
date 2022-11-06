/**
 * @file local_expansion.hpp
 */
#ifndef KOMORI_LOCAL_EXPANSION_HPP_
#define KOMORI_LOCAL_EXPANSION_HPP_

#include <algorithm>
#include <optional>
#include <utility>

#include "bitset.hpp"
#include "board_key_hand_pair.hpp"
#include "delayed_move_list.hpp"
#include "double_count_elimination.hpp"
#include "fixed_size_stack.hpp"
#include "hands.hpp"
#include "initial_estimation.hpp"
#include "move_picker.hpp"
#include "node.hpp"
#include "transposition_table.hpp"

namespace komori {
namespace detail {
/**
 * @brief OR node `n` を `move` した局面が自明な詰み／不詰かどうかを判定する。
 * @param n     現局面
 * @param move  次の手
 * @return `n` を `move` で進めた局面が自明な詰みまたは不詰ならその結果を返す。それ以外なら `std::nullopt` を返す。
 *
 * 末端局面における固定深さ探索。詰め探索で必須ではないが、これによって高速化することができる。
 *
 * 高速 1 手詰めルーチンおよび高速 0 手不詰ルーチンにより自明な詰み／不詰を展開することなく検知することができる。
 */
inline std::optional<SearchResult> CheckObviousFinalOrNode(Node& n) {
  if (!DoesHaveMatePossibility(n.Pos())) {
    const auto hand = HandSet{DisproofHandTag{}}.Get(n.Pos());
    return SearchResult::MakeFinal<false>(hand, kDepthMaxMateLen, 1);
  } else if (auto [best_move, proof_hand] = CheckMate1Ply(n); proof_hand != kNullHand) {
    return SearchResult::MakeFinal<true>(proof_hand, MateLen{1}, 1);
  }
  return std::nullopt;
}
}  // namespace detail

class LocalExpansion {
 private:
  auto MakeComparer() const {
    const SearchResultComparer sr_comparer{or_node_};
    return [this, sr_comparer](std::size_t i_raw, std::size_t j_raw) -> bool {
      const auto& left_result = results_[i_raw];
      const auto& right_result = results_[j_raw];
      const auto ordering = sr_comparer(left_result, right_result);
      if (ordering == SearchResultComparer::Ordering::kLess) {
        return true;
      } else if (ordering == SearchResultComparer::Ordering::kGreater) {
        return false;
      }

      return mp_[i_raw] < mp_[j_raw];
    };
  }

 public:
  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  LocalExpansion(tt::TranspositionTable& tt,
                 const Node& n,
                 MateLen len,
                 bool first_search,
                 BitSet64 sum_mask = BitSet64::Full())
      : or_node_{n.IsOrNode()},
        mp_{n, true},
        delayed_move_list_{n, mp_},
        len_{len},
        key_hand_pair_{n.GetBoardKeyHandPair()},
        sum_mask_{sum_mask} {
    Node& nn = const_cast<Node&>(n);

    std::uint32_t next_i_raw = 0;
    for (const auto& move : mp_) {
      const auto i_raw = next_i_raw++;
      const auto hand_after = n.OrHandAfter(move.move);
      idx_.Push(i_raw);
      auto& result = results_[i_raw];
      auto& query = queries_[i_raw];
      child_key_hand_pairs_[i_raw] = n.BoardKeyHandPairAfter(move.move);

      if (const auto depth_opt = n.IsRepetitionOrInferiorAfter(move.move)) {
        result = SearchResult::MakeRepetition(hand_after, len, 1, *depth_opt);
      } else {
        // 子局面が OR node  -> 1手詰以上
        // 子局面が AND node -> 0手詰以上
        const auto min_len = or_node_ ? MateLen{0} : MateLen{1};
        if (len_ - 1 < min_len) {
          // どう見ても詰まない
          result = SearchResult::MakeFinal<false>(hand_after, min_len - 1, 1);
          goto CHILD_LOOP_END;
        }

        if (!IsSumDeltaNode(n, move.move)) {
          sum_mask_.Reset(i_raw);
        }

        query = tt.BuildChildQuery(n, move.move);
        result = query.LookUp(does_have_old_child_, len - 1, [&n, &move]() { return InitialPnDn(n, move.move); });

        if (!result.IsFinal() && !or_node_ && first_search && result.GetUnknownData().is_first_visit) {
          nn.DoMove(move.move);
          if (auto res = detail::CheckObviousFinalOrNode(nn); res.has_value()) {
            result = *res;
            query.SetResult(*res);
          }
          nn.UndoMove();
        }

        if (!result.IsFinal()) {
          auto next_dep = delayed_move_list_.Prev(i_raw);
          while (next_dep.has_value()) {
            if (!results_[*next_dep].IsFinal()) {
              // i_raw は next_dep の負けが確定した後で探索する
              idx_.Pop();
              break;
            }

            next_dep = delayed_move_list_.Prev(*next_dep);
          }
        }
      }

    CHILD_LOOP_END:
      if (result.Phi(or_node_) == 0) {
        break;
      }
    }

    std::sort(idx_.begin(), idx_.end(), MakeComparer());
    RecalcDelta();
  }

  LocalExpansion(const LocalExpansion&) = delete;
  LocalExpansion(LocalExpansion&&) = delete;
  LocalExpansion& operator=(const LocalExpansion&) = delete;
  LocalExpansion& operator=(LocalExpansion&&) = delete;
  ~LocalExpansion() = default;

  Move BestMove() const { return mp_[idx_.front()].move; }
  bool DoesHaveOldChild() const { return does_have_old_child_; }
  bool FrontIsFirstVisit() const { return FrontResult().GetUnknownData().is_first_visit; }
  BitSet64 FrontSumMask() const {
    const auto& result = FrontResult();
    return result.GetUnknownData().sum_mask;
  }
  bool empty() const noexcept { return idx_.empty(); }

  SearchResult CurrentResult(const Node& n) const {
    if (GetPn() == 0) {
      return GetProvenResult(n);
    } else if (GetDn() == 0) {
      return GetDisprovenResult(n);
    } else {
      return GetUnknownResult(n);
    }
  }

  void UpdateBestChild(const SearchResult& search_result, BoardKeyHandPair parent_key_hand_pair) {
    const auto old_i_raw = idx_[0];
    auto& query = queries_[old_i_raw];
    auto& result = results_[old_i_raw];
    result = search_result;
    query.SetResult(search_result, parent_key_hand_pair);

    if (search_result.Delta(or_node_) == 0 && delayed_move_list_.Next(old_i_raw)) {
      // 後回しにした手があるならそれを復活させる
      // curr_i_raw の次に調べるべき子
      auto curr_i_raw = delayed_move_list_.Next(old_i_raw);
      do {
        idx_.Push(*curr_i_raw);
        if (results_[*curr_i_raw].Delta(or_node_) > 0) {
          // まだ結論の出ていない子がいた
          break;
        }

        // curr_i_raw は結論が出ているので、次の後回しにした手 next_dep を調べる
        curr_i_raw = delayed_move_list_.Next(*curr_i_raw);
      } while (curr_i_raw.has_value());

      std::sort(idx_.begin(), idx_.end(), MakeComparer());
      RecalcDelta();
    } else {
      const bool old_is_sum_delta = sum_mask_[old_i_raw];
      if (old_is_sum_delta) {
        sum_delta_except_best_ += result.Delta(or_node_);
      } else {
        max_delta_except_best_ = std::max(max_delta_except_best_, result.Delta(or_node_));
      }

      ResortFront();

      const auto new_i_raw = idx_[0];
      const auto new_result = results_[new_i_raw];
      const bool new_is_sum_delta = sum_mask_[new_i_raw];
      if (new_is_sum_delta) {
        sum_delta_except_best_ -= new_result.Delta(or_node_);
      } else if (new_result.Delta(or_node_) < max_delta_except_best_) {
        // new_best_child を抜いても max_delta_except_best_ の値は変わらない
      } else {
        // max_delta_ の再計算が必要
        RecalcDelta();
      }
    }
  }

  std::pair<PnDn, PnDn> PnDnThresholds(PnDn thpn, PnDn thdn) const {
    // pn/dn で考えるよりも phi/delta で考えたほうがわかりやすい
    // そのため、いったん phi/delta の世界に変換して、最後にもとに戻す

    const auto thphi = Phi(thpn, thdn, or_node_);
    const auto thdelta = Delta(thpn, thdn, or_node_);
    const auto child_thphi = std::min(thphi, GetSecondPhi() + 1);
    const auto child_thdelta = NewThdeltaForBestMove(thdelta);

    if (or_node_) {
      return {child_thphi, child_thdelta};
    } else {
      return {child_thdelta, child_thphi};
    }
  }

  bool ResolveDoubleCountIfBranchRoot(BranchRootEdge edge) {
    if (edge.branch_root_key_hand_pair == key_hand_pair_) {
      sum_mask_.Reset(idx_.front());
      for (const auto i_raw : idx_) {
        const auto& child_key_hand_pair = child_key_hand_pairs_[i_raw];
        if (child_key_hand_pair == edge.child_key_hand_pair) {
          if (sum_mask_.Test(idx_[i_raw])) {
            sum_mask_.Reset(i_raw);
            RecalcDelta();
          }
          break;
        }
      }
      return true;
    }

    return false;
  }

  bool ShouldStopAncestorSearch(bool branch_root_is_or_node) const {
    if (or_node_ != branch_root_is_or_node) {
      return false;
    }

    const auto& best_result = results_[idx_.front()];
    const PnDn delta_diff = GetDelta() - best_result.Delta(or_node_);
    return delta_diff > kAncestorSearchThreshold;
  }

 private:
  const SearchResult& FrontResult() const { return results_[idx_.front()]; }

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
    return SaturatedAdd(sum_delta, max_delta);
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

    // 後回しにしている子局面が存在する場合、その値をδ値に加算しないと局面を過大評価してしまう。
    //
    // 例） sfen +P5l2/4+S4/p1p+bpp1kp/6pgP/3n1n3/P2NP4/3P1NP2/2P2S3/3K3L1 b RGSL2Prb2gsl3p 159
    //      1筋の合駒を考える時、玉方が合駒を微妙に変えることで読みの深さを指数関数的に大きくできてしまう
    if (mp_.size() > idx_.size()) {
      // 後回しにしている手1つにつき 1/4 点減点する。小数点以下は切り捨てするが、計算結果が 1 を下回る場合のみ
      // 1 に切り上げる。
      sum_delta += std::max<std::size_t>((mp_.size() - idx_.size()) / 4, 1);
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

  PnDn NewThdeltaForBestMove(PnDn thdelta) const {
    PnDn delta_except_best = sum_delta_except_best_;
    if (mp_.size() > idx_.size()) {
      delta_except_best += std::max<std::size_t>((mp_.size() - idx_.size()) / 4, 1);
    }

    if (sum_mask_[idx_[0]]) {
      delta_except_best = SaturatedAdd(delta_except_best, max_delta_except_best_);
    }

    // 計算の際はオーバーフローに注意
    if (thdelta >= delta_except_best) {
      return Clamp(thdelta - delta_except_best);
    }

    return 0;
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

  SearchResult GetProvenResult(const Node& n) const {
    if (or_node_) {
      const auto& result = FrontResult();
      const auto best_move = mp_[idx_[0]];
      const auto proof_hand = BeforeHand(n.Pos(), best_move, result.GetFinalData().hand);
      const auto mate_len = std::min(result.Len() + 1, kDepthMaxMateLen);
      const auto amount = result.Amount();

      return SearchResult::MakeFinal<true>(proof_hand, mate_len, amount);
    } else {
      // 子局面の証明駒の極小集合を計算する
      HandSet set{ProofHandTag{}};
      MateLen mate_len = kZeroMateLen;
      std::uint32_t amount = 1;
      for (const auto i_raw : idx_) {
        const auto& result = results_[i_raw];

        set.Update(result.GetFinalData().hand);
        amount = std::max(amount, result.Amount());
        if (MateLen{result.Len()} + 1 > mate_len) {
          mate_len = std::min(MateLen{result.Len()} + 1, kDepthMaxMateLen);
        }
      }

      // amount の総和を取ると値が大きくなりすぎるので子の数だけ足す
      amount += std::max<std::uint32_t>(idx_.size(), 1) - 1;

      const auto proof_hand = set.Get(n.Pos());
      return SearchResult::MakeFinal<true>(proof_hand, mate_len, amount);
    }
  }

  SearchResult GetDisprovenResult(const Node& n) const {
    // children_ は千日手エントリが手前に来るようにソートされているので、以下のようにして千日手判定ができる
    if (!mp_.empty()) {
      if (const auto& result = FrontResult(); result.GetFinalData().IsRepetition()) {
        const auto depth = result.GetFinalData().repetition_start;
        if (depth < n.GetDepth()) {
          return SearchResult::MakeRepetition(n.OrHand(), len_, 1, depth);
        } else {
          return SearchResult::MakeFinal<false>(n.OrHand(), len_, 1);
        }
      }
    }

    // フツーの不詰
    if (or_node_) {
      // 子局面の反証駒の極大集合を計算する
      HandSet set{DisproofHandTag{}};
      MateLen mate_len = kDepthMaxMateLen;
      std::uint32_t amount = 1;
      for (const auto i_raw : idx_) {
        const auto& result = results_[i_raw];
        const auto child_move = mp_[i_raw];

        set.Update(BeforeHand(n.Pos(), child_move, result.GetFinalData().hand));
        amount = std::max(amount, result.Amount());
        if (result.Len() + 1 < mate_len) {
          mate_len = result.Len() + 1;
        }
      }
      amount += std::max<std::uint32_t>(idx_.size(), 1) - 1;
      const auto disproof_hand = set.Get(n.Pos());

      return SearchResult::MakeFinal<false>(disproof_hand, mate_len, amount);
    } else {
      const auto& result = FrontResult();
      auto disproof_hand = result.GetFinalData().hand;
      const auto mate_len = std::min(result.Len() + 1, kDepthMaxMateLen);
      const auto amount = result.Amount();

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

      return SearchResult::MakeFinal<false>(disproof_hand, mate_len, amount);
    }
  }

  SearchResult GetUnknownResult(const Node& /* n */) const {
    const auto& result = FrontResult();
    const std::uint32_t amount = result.Amount() + idx_.size() / 2;

    const UnknownData unknown_data{false, sum_mask_};
    return SearchResult::MakeUnknown(GetPn(), GetDn(), len_, amount, unknown_data);
  }

  void ResortFront() {
    const auto comparer = MakeComparer();

    auto itr = std::lower_bound(idx_.begin() + 1, idx_.end(), idx_[0], comparer);
    std::rotate(idx_.begin(), idx_.begin() + 1, itr);
  }

  const bool or_node_;
  const MovePicker mp_;
  const DelayedMoveList delayed_move_list_;
  const MateLen len_;
  const BoardKeyHandPair key_hand_pair_;

  std::array<SearchResult, kMaxCheckMovesPerNode> results_;
  std::array<tt::Query, kMaxCheckMovesPerNode> queries_;
  std::array<BoardKeyHandPair, kMaxCheckMovesPerNode> child_key_hand_pairs_;

  bool does_have_old_child_{false};

  PnDn sum_delta_except_best_;
  PnDn max_delta_except_best_;

  BitSet64 sum_mask_;
  FixedSizeStack<std::uint32_t, kMaxCheckMovesPerNode> idx_;
};
}  // namespace komori

#endif  // KOMORI_LOCAL_EXPANSION_HPP_
