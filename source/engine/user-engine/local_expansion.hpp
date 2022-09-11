#ifndef KOMORI_LOCAL_EXPANSION_HPP_
#define KOMORI_LOCAL_EXPANSION_HPP_

#include <algorithm>
#include <optional>
#include <utility>

#include "bitset.hpp"
#include "delayed_move_list.hpp"
#include "fixed_size_stack.hpp"
#include "hands.hpp"
#include "initial_estimation.hpp"
#include "move_picker.hpp"
#include "node.hpp"
#include "tt.hpp"

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
    return SearchResult::MakeFinal<false>(hand, kMaxMateLen, 1);
  } else if (auto [best_move, proof_hand] = CheckMate1Ply(n); proof_hand != kNullHand) {
    const auto proof_hand_after = AfterHand(n.Pos(), best_move, proof_hand);
    const auto len = MateLen::Make(1, MateLen::kFinalHandMax);
    return SearchResult::MakeFinal<true>(proof_hand, len, 1);
  }
  return std::nullopt;
}
}  // namespace detail

class LocalExpansion {
 private:
  auto MakeComparer() const {
    SearchResultComparer sr_comparer{or_node_};
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
  LocalExpansion(tt::TranspositionTable& tt,
                 const Node& n,
                 MateLen len,
                 bool first_search,
                 BitSet64 sum_mask = BitSet64::Full(),
                 LocalExpansion* parent = nullptr)
      : or_node_{n.IsOrNode()},
        mp_{n, true},
        delayed_move_list_{n, mp_},
        len_{len},
        sum_mask_{sum_mask},
        parent_{parent},
        board_key_{n.BoardKey()},
        or_hand_{n.OrHand()} {
    Node& nn = const_cast<Node&>(n);

    std::uint32_t next_i_raw = 0;
    for (const auto& move : mp_) {
      const auto i_raw = next_i_raw++;
      const auto hand_after = n.OrHandAfter(move.move);
      idx_.Push(i_raw);
      auto& result = results_[i_raw];
      auto& query = queries_[i_raw];

      if (n.IsRepetitionOrInferiorAfter(move.move)) {
        result.InitFinal<false, true>(hand_after, len, 1);
      } else {
        const auto min_len =
            or_node_ ? MateLen::Make(1, MateLen::kFinalHandMax) : MateLen::Make(2, MateLen::kFinalHandMax);
        if (len_ < min_len) {
          result.InitFinal<false>(hand_after, (min_len - 1).Prec(), 1);
          goto CHILD_LOOP_END;
        }

        if (!IsSumDeltaNode(n, move.move)) {
          sum_mask_.Reset(i_raw);
        }

        query = tt.BuildChildQuery(n, move.move);
        result =
            query.LookUp(does_have_old_child_, len - 1, false, [&n, &move]() { return InitialPnDn(n, move.move); });

        if (!result.IsFinal() && !or_node_ && first_search && result.GetUnknownData().is_first_visit) {
          nn.DoMove(move.move);
          if (auto res = detail::CheckObviousFinalOrNode(nn); res.has_value()) {
            result = *res;
            query.SetResult(*res);
          }
          nn.UndoMove();
        }

        if (!result.IsFinal() && delayed_move_list_.Prev(i_raw)) {
          idx_.Pop();
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
    return BitSet64{~result.GetUnknownData().secret};
  }

  SearchResult CurrentResult(const Node& n) const {
    if (GetPn() == 0) {
      return GetProvenResult(n);
    } else if (GetDn() == 0) {
      return GetDisprovenResult(n);
    } else {
      return GetUnknownResult(n);
    }
  }

  void UpdateBestChild(const SearchResult& search_result) {
    const auto old_i_raw = idx_[0];
    auto& query = queries_[old_i_raw];
    auto& result = results_[old_i_raw];
    result = search_result;
    query.SetResult(search_result);

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

  PnDn NewThdeltaForBestMove(PnDn thdelta) const {
    PnDn delta_except_best = sum_delta_except_best_;
    if (sum_mask_[idx_[0]]) {
      delta_except_best += max_delta_except_best_;
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
      const auto proof_hand = BeforeHand(n.Pos(), best_move, result.GetHand());
      const auto mate_len = std::min(result.Len() + 1, kMaxMateLen);
      const auto amount = result.Amount();

      return SearchResult::MakeFinal<true>(proof_hand, mate_len, amount);
    } else {
      // 子局面の証明駒の極小集合を計算する
      HandSet set{ProofHandTag{}};
      MateLen mate_len = kZeroMateLen;
      std::uint32_t amount = 1;
      for (const auto i_raw : idx_) {
        const auto& result = results_[i_raw];

        set.Update(result.GetHand());
        amount = std::max(amount, result.Amount());
        if (MateLen{result.Len()} + 1 > mate_len) {
          mate_len = std::min(MateLen{result.Len()} + 1, kMaxMateLen);
        }
      }

      const auto proof_hand = set.Get(n.Pos());

      // amount の総和を取ると値が大きくなりすぎるので子の数だけ足す
      amount += std::max(mp_.size(), std::size_t{1}) - 1;

      if (idx_.empty()) {
        mate_len = MateLen::Make(0, MateLen::kFinalHandMax);
        if (mate_len > len_) {
          return SearchResult::MakeFinal<false>(n.OrHand(), mate_len.Prec(), amount);
        }
      }
      return SearchResult::MakeFinal<true>(proof_hand, mate_len, amount);
    }
  }

  SearchResult GetDisprovenResult(const Node& n) const {
    // children_ は千日手エントリが手前に来るようにソートされているので、以下のようにして千日手判定ができる
    if (!mp_.empty()) {
      if (const auto& result = FrontResult(); result.GetFinalData().is_repetition) {
        return SearchResult::MakeFinal<false, false>(n.OrHand(), len_, 1);
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

        set.Update(BeforeHand(n.Pos(), child_move, result.GetHand()));
        amount = std::max(amount, result.Amount());
        if (result.Len() + 1 < mate_len) {
          mate_len = result.Len() + 1;
        }
      }
      amount += std::max(mp_.size(), std::size_t{1}) - 1;
      const auto disproof_hand = set.Get(n.Pos());

      return SearchResult::MakeFinal<false>(disproof_hand, mate_len, amount);
    } else {
      const auto& result = FrontResult();
      auto disproof_hand = result.GetHand();
      const auto mate_len = std::min(result.Len() + 1, kMaxMateLen);
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

  SearchResult GetUnknownResult(const Node& n) const {
    const auto& result = FrontResult();
    const std::uint32_t amount = result.Amount() + mp_.size() / 2;

    Key parent_board_key{kNullKey};
    Hand parent_hand{kNullHand};
    if (parent_ != nullptr) {
      parent_board_key = parent_->board_key_;
      parent_hand = parent_->or_hand_;
    }

    UnknownData unknown_data{false, parent_board_key, parent_hand, ~sum_mask_.Value()};
    return SearchResult::MakeUnknown(GetPn(), GetDn(), or_hand_, len_, amount, unknown_data);
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

  LocalExpansion* const parent_;
  const Key board_key_;
  const Hand or_hand_;

  std::array<SearchResult, kMaxCheckMovesPerNode> results_;
  std::array<tt::Query, kMaxCheckMovesPerNode> queries_;

  bool does_have_old_child_{false};

  PnDn sum_delta_except_best_;
  PnDn max_delta_except_best_;

  BitSet64 sum_mask_;
  FixedSizeStack<std::uint32_t, kMaxCheckMovesPerNode> idx_;
};
}  // namespace komori

#endif  // KOMORI_LOCAL_EXPANSION_HPP_
