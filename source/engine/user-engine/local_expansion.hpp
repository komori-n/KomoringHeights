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
#include "double_count_elimination.hpp"
#include "fixed_size_stack.hpp"
#include "hands.hpp"
#include "initial_estimation.hpp"
#include "move_picker.hpp"
#include "node.hpp"
#include "ranges.hpp"
#include "transposition_table.hpp"
#include "typedefs.hpp"

namespace komori {
namespace detail {
/// 強制的に max 値によるδ値計算へ切り替えるしきい値。
constexpr PnDn kForceSumPnDn = kInfinitePnDn / 1024;

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

/**
 * @brief 局面の局所展開結果を保持する。
 *
 * 現局面の子ノードの展開結果を一時的に保持するクラス。前回の置換表 LookUp 結果を保持しておくことで、
 * 置換表 LookUp 回数をへらすことが目的である。また、高速 2 手詰めルーチンを内蔵したりδ値を事前計算しておいたりなど、
 * 高速化のために小手先高速化が詰め込まれている。
 *
 * ## 実装詳細
 *
 * ### 生添字（i_raw）
 *
 * 同じ局面 `n` に対し、`MovePicker` は常に同じ順番の指し手を生成する。つまり、`MovePicker` の添字により指し手を
 * 一意に特定できる。このように、任意の指し手 `m` に対し、`MovePicker` における添字を `m` の生添字（raw index）と呼び、
 * 変数 `i_raw` により表現する。
 *
 * ### 添字スタック（idx_）
 *
 * 探索中に「良さげ順」に生添字の並び替えを行いたい。このような生添字のリストが添え字スタック `idx`である。
 * 探索結果の配列 `results_` を直接並び替えるのではなく `idx_` を並び替えることで、並び替えにかかる命令数を
 * 削減することができる。また、添字スタックを経由してアクセスすることで、`mp_` や `sum_mask_` を「良さげ順」で
 * アクセスすることができる。
 *
 * スタック構造を活かして探索中に `idx_` へ生添字を追加することもできる。これは、
 * 指し手の遅延展開（`delayed_move_list_`）に用いられる。
 *
 * ### δ値の計算（sum_delta_except_best_, max_delta_except_best_, sum_mask）
 *
 * 現局面のδ値は、和で計上する子の集合 sum_child と最大値で計上する子の集合 max_child を用いて
 *    δ = Σ_[i in sum_child] δ_i + max_[i in max_child] δ_i
 * により計算できる。また、子ノードにおける探索取りやめのδ値のしきい値 thdelta_i は、
 * 「現局面のδしきい値 thdelta をこえるようなδ_i の最大値」により定義される。
 *
 * まともに計算すると、「δ値」と「子のδしきい値」で2回でこの計算が必要になる。この計算は
 * 探索中にとても頻繁に現れる計算であるため、できる限り計算量を削減したい。そのため、
 * 「最善手を除いた sum_child のδ値の和 `sum_delta_except_best_`」と「最善手を除いた max_child の
 * δ値の最大値 `max_delta_except_best_`」を事前に計算しておく。これら2つの変数があれば、
 * 「δ値」と「子のδしきい値」をどちらも短時間で計算できる。また、これらの値は多くの場合において、
 * 差分計算で効率よく更新できることもポイントである。
 *
 * δ値を和で計上するか最大値で計上するかは `sum_mask_` で管理している。`sum_mask_` のビットが立っている子は
 * 和で、立っていない子は最大値でδ値を計上する。「和」の方にビットを立てるようにしている理由は、
 * 仮に子の個数が 64 個を超える場合は最大値で計上したいから。
 *
 * 基本的には legacy df-pn のように和で計上するが、`IsSumDeltaNode()` に当てはまるノードと二重カウント検出された子は
 * 最大値で計算することでδ値の発散を抑える。
 *
 * ### MultiPV
 *
 * multi_pv > 1 のとき、勝ちになる手が見つかった後も multi_pv 個の勝ちが見つかるまでは探索を続ける。ここで、
 * 勝ちになる手とは OR node では詰む手、AND node では不詰になる手のことである。
 *
 * 勝ちになる手は、以降の探索から除外される。除外されている手の個数は `excluded_moves_` で管理されている。
 * `BestMove()` や `FrontResult()` で現時点の最善手を取得するとき、除外された手は最善手に含まれないので注意すること。
 */
class LocalExpansion {
 private:
  /**
   * @brief  `idx_` の比較器を生成する。
   * @return 関数オブジェクト（比較器）
   * @note ラムダ式を返すために、戻り値を auto にしてクラス先頭で定義している。
   */
  auto MakeComparer() const {
    const SearchResultComparer sr_comparer{or_node_};
    return [this, sr_comparer](std::size_t i_raw, std::size_t j_raw) -> bool {
      // `SearchResultComparer` で大小比較の決着がつくならそれに従う。
      // `SearchResultComparer` で結論がでなければ、指し手自体の評価値（指し手生成時に付与）で大小を決める。
      const auto& left_result = results_[i_raw];
      const auto& right_result = results_[j_raw];
      const auto ordering = sr_comparer(left_result, right_result);
      if (ordering == SearchResultComparer::Ordering::kLess) {
        return true;
      } else if (ordering == SearchResultComparer::Ordering::kGreater) {
        return false;
      }

      return mp_[i_raw].value < mp_[j_raw].value;
    };
  }

 public:
  /**
   * @brief LocalExpansion を構築する。
   * @param tt  置換表
   * @param n   現局面
   * @param len 残り詰み手数
   * @param first_search 初回探索なら `true`。`true` なら高速 1 手詰めルーチンを走らせる。
   * @param sum_mask δ値を和で計算する子の集合
   * @param multi_pv 勝ちになる手をいくつ見つけるか。1以上でなければならない
   */ // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  LocalExpansion(tt::TranspositionTable& tt,
                 const Node& n,
                 MateLen len,
                 bool first_search,
                 BitSet64 sum_mask = BitSet64::Full(),
                 std::uint32_t multi_pv = 1)
      : or_node_{n.IsOrNode()}, mp_{n, true}, len_{len}, key_hand_pair_{n.GetBoardKeyHandPair()}, multi_pv_{multi_pv} {
    (void)sum_mask;
    // 1手詰め／1手不詰判定のために、const を一時的に外す
    Node& nn = const_cast<Node&>(n);

    for (const auto& [i_raw, move] : WithIndex<std::uint32_t>(mp_)) {
      const auto hand_after = n.OrHandAfter(move.move);
      idx_.Push(i_raw);
      auto& result = results_[i_raw];
      auto& query = queries_[i_raw];

      if (const auto depth_opt = n.IsRepetitionOrInferiorAfter(move.move)) {
        result = SearchResult::MakeRepetition(hand_after, len, 1, *depth_opt);
      } else {
        // 子局面が OR node  -> 1手詰以上
        // 子局面が AND node -> 0手詰以上
        const auto min_len = or_node_ ? MateLen{0} : MateLen{1};
        if (len_ < min_len + 1) {
          // どう見ても詰まない
          result = SearchResult::MakeFinal<false>(hand_after, min_len - 1, 1);
          goto CHILD_LOOP_END;
        }

        query = tt.BuildChildQuery(n, move.move);
        result = query.LookUp(does_have_old_child_, len - 1,
                              [&n, &move = move]() { return std::make_pair(kPnDnUnit, kPnDnUnit); });

        if (!result.IsFinal()) {
          if (!or_node_ && first_search && result.GetUnknownData().is_first_visit) {
            nn.DoMove(move.move);
            if (auto res = detail::CheckObviousFinalOrNode(nn); res.has_value()) {
              result = *res;
              query.SetResult(*res);
            }
            nn.UndoMove();
          }
        }
      }

    CHILD_LOOP_END:
      if (result.Phi(or_node_) == 0) {
        if (excluded_moves_ >= multi_pv_ - 1) {
          break;
        }
        excluded_moves_++;
      }

      if (!result.IsFinal()) {
        if (is_drop(move.move)) {
          num_drop_moves_++;
        } else {
          num_nondrop_moves_++;
        }
      }
    }

    std::sort(idx_.begin(), idx_.end(), MakeComparer());
    RecalcDelta();
  }

  /// Copy constructor(delete)
  LocalExpansion(const LocalExpansion&) = delete;
  /// Move constructor(delete)
  LocalExpansion(LocalExpansion&&) = delete;
  /// Copy assign operator(delete)
  LocalExpansion& operator=(const LocalExpansion&) = delete;
  /// Move assign operator(delete)
  LocalExpansion& operator=(LocalExpansion&&) = delete;
  /// Destructor(default)
  ~LocalExpansion() = default;

  /**
   * @brief 合法手がないかどうか
   * @see CurrentResult
   */
  bool empty() const noexcept { return idx_.empty(); }
  /**
   * @brief 現時点の最善手
   * @pre !Current().IsFinal()
   */
  Move BestMove() const { return mp_[idx_[excluded_moves_]].move; }
  /**
   * @brief 最善の子の探索結果を取得する
   * @pre !Current().IsFinal()
   */
  const SearchResult& FrontResult() const { return results_[idx_[excluded_moves_]]; }
  /**
   * @brief unproven old child がいるかどうか
   */
  bool DoesHaveOldChild() const { return does_have_old_child_; }
  /**
   * @brief 最善手の子ノードが初探索かどうか
   * @pre !CurrentResult().IsFinal()
   */
  bool FrontIsFirstVisit() const { return FrontResult().GetUnknownData().is_first_visit; }
  /**
   * @brief 最善手の Sum Mask
   * @pre !CurrentResult().IsFinal()
   */
  BitSet64 FrontSumMask() const {
    const auto& result = FrontResult();
    return result.GetUnknownData().sum_mask;
  }

  /**
   * @brief (Move, SearchResult) のペアを良さげ順にすべて取得する
   */
  auto GetAllResults() const {
    return Zip(Apply(idx_, [this](const std::size_t i_raw) { return mp_[i_raw].move; }),
               Apply(idx_, [this](const std::size_t i_raw) { return results_[i_raw]; }));
  }

  /**
   * @brief 現局面における探索結果を返す
   * @param n 現局面
   * @pre `n` がコンストラクト時に渡されたものと同じ局面
   * @note `!CurrentResult(n).IsFinal()` ならば `!empty()` が成り立つ。これを利用して `!empty()` のチェックを
   * スキップすることがある。
   */
  SearchResult CurrentResult(const Node& n) const {
    if (GetPhi() == 0) {
      return GetWinResult(n);
    } else if (GetDelta() == 0) {
      return GetLoseResult(n);
    } else {
      return GetUnknownResult(n);
    }
  }

  /**
   * @brief 最善手の子の評価値を更新する
   * @param search_result 最善手の子の評価値
   * @pre !empty()
   */
  void UpdateBestChild(const SearchResult& search_result) {
    const auto old_i_raw = idx_[excluded_moves_];
    const auto& query = queries_[old_i_raw];
    auto& result = results_[old_i_raw];

    result = search_result;
    query.SetResult(search_result, key_hand_pair_);
    if (search_result.Phi(or_node_) == 0) {
      // 後から見つかった手のほうがいい手かもしれないので、前半部分をソートし直しておく
      ResortExcludedBack();
      if (excluded_moves_ >= multi_pv_ - 1) {
        // multi_pv_ 個の勝ちになる手を見つけたので、これ以上探索を続ける必要はない
        return;
      }
      excluded_moves_++;
      if (excluded_moves_ >= mp_.size()) {
        // 全合法手が勝ちだとわかったので、これ以上探索を続ける必要はない
        return;
      }
    }

    if (search_result.Phi(or_node_) > 0) {
      delta_except_best_ = std::max(delta_except_best_, search_result.Delta(or_node_));
    } else if (search_result.IsFinal()) {
      if (is_drop(mp_[old_i_raw])) {
        num_drop_moves_--;
      } else {
        num_nondrop_moves_--;
      }
    }
    ResortFront();

    const auto new_i_raw = idx_[excluded_moves_];
    const auto new_result = results_[new_i_raw];
    if (new_result.Delta(or_node_) < delta_except_best_) {
      // new_best_child を抜いても max_delta_except_best_ の値は変わらない
    } else {
      // max_delta_ の再計算が必要
      RecalcDelta();
    }
  }

  /**
   * @brief 最善手の子の探索で用いる pn と dn のしきい値
   * @pre !empty()
   * @param thpn 現局面の pn のしきい値
   * @param thdn 現局面の dn のしきい値
   * @return 子局面の pn, dn のしきい値のペア
   */
  std::pair<PnDn, PnDn> FrontPnDnThresholds(PnDn thpn, PnDn thdn) const {
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

  /**
   * @brief 二重カウントの起点 `edge` に一致する枝が子に含まれるなら二重カウントを解消する
   * @param edge 二重カウントの起点の枝
   * @return `edge` を見つけたら true
   */
  bool ResolveDoubleCountIfBranchRoot(BranchRootEdge edge) { return false; }

  /**
   * @brief 二重カウント探索をやめるべきかどうか判定する
   * @param branch_root_is_or_node 二重カウントの分岐元が OR node かどうか
   */
  bool ShouldStopAncestorSearch(bool branch_root_is_or_node) const {
    if (or_node_ != branch_root_is_or_node) {
      return false;
    }

    const auto& best_result = FrontResult();
    const PnDn delta_diff = GetDelta() - best_result.Delta(or_node_);
    return delta_diff > kAncestorSearchThreshold;
  }

 private:
  // <PnDn>
  /// Pn を計算する
  PnDn GetPn() const {
    if (or_node_) {
      return GetPhi();
    } else {
      return GetDelta();
    }
  }

  /// Dn を計算する
  PnDn GetDn() const {
    if (or_node_) {
      return GetDelta();
    } else {
      return GetPhi();
    }
  }

  /// phi 値を計算する
  PnDn GetPhi() const {
    PnDn front_phi = 0;
    if (excluded_moves_ < idx_.size()) {
      front_phi = FrontResult().Phi(or_node_);
    } else {
      front_phi = kInfinitePnDn;
    }

    if (front_phi >= kInfinitePnDn && excluded_moves_ > 0) {
      return 0;
    }
    return front_phi;
  }

  /// delta 値を計算する
  PnDn GetDelta() const {
    if (idx_.empty()) {
      return 0;
    }

    const auto& best_result = FrontResult();
    const auto delta_base = std::max(delta_except_best_, best_result.Delta(or_node_));
    if (delta_base == 0) {
      return 0;
    }
    if (or_node_) {
      return ClampPnDn(delta_base + kPnDnUnit * (num_drop_moves_ + num_nondrop_moves_ - 1));
    } else {
      return ClampPnDn(delta_base + kPnDnUnit * ((num_drop_moves_ > 0 ? 1 : 0) + num_nondrop_moves_ - 1));
    }
  }

  /// 2番目の子の phi 値を計算する
  constexpr PnDn GetSecondPhi() const {
    if (idx_.size() <= excluded_moves_ + 1) {
      return kInfinitePnDn;
    }
    const auto& second_best_result = results_[idx_[excluded_moves_ + 1]];
    return second_best_result.Phi(or_node_);
  }

  /**
   * @brief 現局面の delta しきい値が `thdelta` のとき、子局面の delta しきい値を計算する
   * @param thdelta 現局面の delta しきい値
   */
  PnDn NewThdeltaForBestMove(PnDn thdelta) const {
    if (or_node_) {
      if (thdelta > kPnDnUnit * (num_drop_moves_ + num_nondrop_moves_ - 1)) {
        return thdelta - kPnDnUnit * (num_drop_moves_ + num_nondrop_moves_ - 1);
      }
    } else {
      if (thdelta > kPnDnUnit * ((num_drop_moves_ > 0 ? 1 : 0) + num_nondrop_moves_ - 1)) {
        return thdelta - kPnDnUnit * ((num_drop_moves_ > 0 ? 1 : 0) + num_nondrop_moves_ - 1);
      }
    }
    return 0;
  }
  // </PnDn>

  /**
   * @brief δ値の一時変数 `sum_delta_except_best_`, `max_delta_except_best_` を計算し直す
   */
  constexpr void RecalcDelta() {
    delta_except_best_ = 0;

    for (const auto& i_raw : Skip(idx_, excluded_moves_ + 1)) {
      const auto delta_i = results_[i_raw].Delta(or_node_);
      delta_except_best_ = std::max(delta_except_best_, delta_i);
    }
  }

  /// 探索結果を取得する（手番側から見て勝ち局面）
  SearchResult GetWinResult(const Node& n) const {
    // excluded_moves_ に関係なく最も良い手がほしいので FrontResult() は使えない
    const auto& result = results_[idx_.front()];
    const auto best_move = mp_[idx_[0]];
    const auto mate_len = result.Len() + 1;
    const auto amount = result.Amount() + mp_.size() - 1;
    const auto after_hand = result.GetFinalData().hand;

    if (or_node_) {
      const auto proof_hand = BeforeHand(n.Pos(), best_move, after_hand);
      return SearchResult::MakeFinal<true>(proof_hand, mate_len, amount);
    } else {
      auto disproof_hand = after_hand;

      // 駒打ちならその駒を持っていないといけない
      if (is_drop(best_move)) {
        const auto pr = move_dropped_piece(best_move);
        const auto pr_cnt = hand_count(MergeHand(n.OrHand(), n.AndHand()), pr);
        const auto disproof_pr_cnt = hand_count(disproof_hand, pr);
        if (pr_cnt - disproof_pr_cnt <= 0) {
          // もし現局面の攻め方の持ち駒が disproof_hand だった場合、打とうとしている駒 pr が攻め方に独占されているため
          // 受け方は BestMove() を着手することができない。そのため、攻め方の持ち駒を1枚受け方に渡す必要がある。

          // 現局面で OR node + AND node の pr の持ち駒枚数を N とすると、OR node 側の pr の枚数が N-1 になるように
          // disproof_hand を調整する。このとき、disproof_hand の pr 枚数が N より大きい可能性があるので注意。
          sub_hand(disproof_hand, pr, disproof_pr_cnt - pr_cnt + 1);
        }
      }

      // 千日手のときは MakeRepetition で返す
      if (result.GetFinalData().IsRepetition()) {
        const auto depth = result.GetFinalData().repetition_start;
        if (depth < n.GetDepth()) {
          return SearchResult::MakeRepetition(n.OrHand(), mate_len, amount, depth);
        }
      }
      return SearchResult::MakeFinal<false>(disproof_hand, mate_len, amount);
    }
  }

  /// 探索結果を取得する（手番側から見て負け局面）
  SearchResult GetLoseResult(const Node& n) const {
    // amount の総和を取ると値が大きくなりすぎるので子の数だけ足す
    // なお、子の個数が空の場合があるので注意。

    if (or_node_) {
      // 子局面の反証駒の極大集合を計算する
      HandSet set{DisproofHandTag{}};
      MateLen mate_len = len_;
      SearchAmount amount = 1;
      for (const auto i_raw : idx_) {
        const auto& result = results_[i_raw];
        const auto child_move = mp_[i_raw];
        const auto child_disproof_hand = BeforeHand(n.Pos(), child_move, result.GetFinalData().hand);

        set.Update(child_disproof_hand);
        amount = std::max(amount, result.Amount());
        if (result.Len() < mate_len) {
          mate_len = result.Len();
        }
      }
      amount += std::max<SearchAmount>(mp_.size(), 1) - 1;

      // 千日手のときは MakeRepetition で返す
      if (!mp_.empty()) {
        // children_ は千日手エントリが手前に来るようにソートされているので、以下のようにして千日手判定ができる
        if (const auto& result = results_[idx_.front()]; result.GetFinalData().IsRepetition()) {
          const auto depth = result.GetFinalData().repetition_start;
          if (depth < n.GetDepth()) {
            return SearchResult::MakeRepetition(n.OrHand(), mate_len + 1, amount, depth);
          }
        }
      }
      const auto disproof_hand = set.Get(n.Pos());
      return SearchResult::MakeFinal<false>(disproof_hand, mate_len + 1, amount);
    } else {
      // 子局面の証明駒の極小集合を計算する
      HandSet set{ProofHandTag{}};
      MateLen mate_len = kMinus1MateLen;
      SearchAmount amount = 1;
      for (const auto i_raw : idx_) {
        const auto& result = results_[i_raw];

        set.Update(result.GetFinalData().hand);
        amount = std::max(amount, result.Amount());
        if (result.Len() > mate_len) {
          mate_len = result.Len();
        }
      }
      amount += std::max<SearchAmount>(mp_.size(), 1) - 1;

      const auto proof_hand = set.Get(n.Pos());
      return SearchResult::MakeFinal<true>(proof_hand, mate_len + 1, amount);
    }
  }

  /// 探索結果を取得する（不明局面）
  SearchResult GetUnknownResult(const Node& /* n */) const {
    const auto& result = FrontResult();
    const SearchAmount amount = result.Amount() + mp_.size() - 1;
    return SearchResult::MakeUnknown(GetPn(), GetDn(), len_, amount, BitSet64::Full());
  }

  /**
   * @brief 先頭以外がソートされた状態のとき、先頭要素を適切な位置に挿入する
   */
  void ResortFront() {
    if (idx_.size() > excluded_moves_ + 1) {
      const auto comparer = MakeComparer();
      const auto begin = idx_.begin() + excluded_moves_;
      const auto itr = std::lower_bound(begin + 1, idx_.end(), idx_[excluded_moves_], comparer);
      std::rotate(begin, begin + 1, itr);
    }
  }

  /**
   * @brief 末尾以外がソートされた状態のとき、末尾要素を適切な位置に挿入する
   */
  void ResortBack() {
    if (idx_.size() > excluded_moves_ + 1) {
      const auto comparer = MakeComparer();
      const auto begin = idx_.begin() + excluded_moves_;
      const auto itr = std::lower_bound(begin, idx_.end() - 1, idx_.back(), comparer);
      std::rotate(itr, idx_.end() - 1, idx_.end());
    }
  }

  /**
   * @brief [0, excluded_moves_] の末尾以外がソートされた状態のとき、末尾要素を適切な位置に挿入する
   */
  void ResortExcludedBack() {
    if (excluded_moves_ > 0) {
      const auto comparer = MakeComparer();
      const auto end = idx_.begin() + excluded_moves_ + 1;
      const auto itr = std::lower_bound(idx_.begin(), end - 1, idx_[excluded_moves_], comparer);
      std::rotate(itr, end - 1, end);
    }
  }

  const bool or_node_;                    ///< 現局面が OR node かどうか
  const MovePicker mp_;                   ///< 現局面の合法手
  const MateLen len_;                     ///< 現局面における残り探索手数
  const BoardKeyHandPair key_hand_pair_;  ///< 現局面の盤面ハッシュ値と持ち駒。二重カウント対策で用いる。
  const std::uint32_t multi_pv_;  ///< MultiPv の値。1以上でなければならない

  /// 子の現在の評価値結果一覧
  std::array<SearchResult, kMaxCheckMovesPerNode> results_;
  /// 子のクエリ一覧。コンストラクト時に作ったクエリを使い回すことで高速化できる
  std::array<tt::Query, kMaxCheckMovesPerNode> queries_;

  /// 現局面の評価値が古い探索情報に基づくものかどうか。TCA の探索延長の判断に用いる。
  bool does_have_old_child_{false};

  PnDn delta_except_best_;
  std::uint32_t num_drop_moves_{};
  std::uint32_t num_nondrop_moves_{};

  /// 現在有効な生添字の一覧。「良さ順」で並んでいる。
  FixedSizeStack<std::uint32_t, kMaxCheckMovesPerNode> idx_;

  /// 勝ちになる手を見つけた個数
  /// multi_pv_ == 1 のときは、この値は常に 0 である。multi_pv_ > 1 のとき、勝ち（phi==0）を見つけた後に探索を続ける
  /// 際に用いる。常に excluded_moves_ <= multi_pv_ - 1 かつ excluded_moves_ <= mp_.size() である。
  std::uint32_t excluded_moves_{0};
};
}  // namespace komori

#endif  // KOMORI_LOCAL_EXPANSION_HPP_
