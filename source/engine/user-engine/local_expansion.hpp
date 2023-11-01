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
#include "ranges.hpp"
#include "transposition_table.hpp"

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
      : or_node_{n.IsOrNode()},
        mp_{n, true},
        delayed_move_list_{n, mp_},
        len_{len},
        key_hand_pair_{n.GetBoardKeyHandPair()},
        multi_pv_{multi_pv},
        sum_mask_{sum_mask} {
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
        result =
            query.LookUp(does_have_old_child_, len - 1, [&n, &move = move]() { return InitialPnDn(n, move.move); });

        if (!result.IsFinal() && !or_node_ && first_search && result.GetUnknownData().is_first_visit) {
          nn.DoMove(move.move);
          if (auto res = detail::CheckObviousFinalOrNode(nn); res.has_value()) {
            result = *res;
            query.SetResult(*res);
          }
          nn.UndoMove();
        }

        if (!result.IsFinal()) {
          if (!IsSumDeltaNode(n, move.move) || result.Delta(or_node_) >= detail::kForceSumPnDn) {
            sum_mask_.Reset(i_raw);
          }

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
        if (excluded_moves_ >= multi_pv_ - 1) {
          break;
        }
        excluded_moves_++;
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

  std::vector<std::pair<Move, SearchResult>> GetAllResults() const {
    std::vector<std::pair<Move, SearchResult>> ret;
    ret.reserve(idx_.size());
    for (const auto i_raw : idx_) {
      ret.push_back(std::make_pair(mp_[i_raw].move, results_[i_raw]));
    }

    return ret;
  }

  /**
   * @brief 現局面における探索結果を返す
   * @param n 現局面
   * @pre `n` がコンストラクト時に渡されたものと同じ局面
   * @note `!CurrentResult(n).IsFinal()` ならば `!empty()` が成り立つ。これを利用して `!empty()` のチェックを
   * スキップすることがある。
   */
  SearchResult CurrentResult(const Node& n) const {
    if (GetPn() == 0) {
      return GetProvenResult(n);
    } else if (GetDn() == 0) {
      return GetDisprovenResult(n);
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
    if (!result.IsFinal() && result.Delta(or_node_) >= detail::kForceSumPnDn) {
      sum_mask_.Reset(old_i_raw);
    }

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

    if (search_result.IsFinal() && delayed_move_list_.Next(old_i_raw)) {
      if (search_result.Delta(or_node_) == 0) {
        // delta==0 の手は最悪手なので並び替えで最後尾へ移動させる
        ResortFront();
      }

      // 後回しにした手があるならそれを復活させる
      // curr_i_raw の次に調べるべき子
      auto curr_i_raw = delayed_move_list_.Next(old_i_raw);
      do {
        idx_.Push(*curr_i_raw);
        ResortBack();
        if (results_[*curr_i_raw].Delta(or_node_) > 0) {
          // まだ結論の出ていない子がいた
          break;
        }

        // curr_i_raw は結論が出ているので、次の後回しにした手 next_dep を調べる
        curr_i_raw = delayed_move_list_.Next(*curr_i_raw);
      } while (curr_i_raw.has_value());

      RecalcDelta();
    } else {
      if (search_result.Phi(or_node_) > 0) {
        // 現在探索していた手が delta_except_best_ に加わるので差分計算する
        const bool old_is_sum_delta = sum_mask_[old_i_raw];
        if (old_is_sum_delta) {
          sum_delta_except_best_ += result.Delta(or_node_);
        } else {
          max_delta_except_best_ = std::max(max_delta_except_best_, result.Delta(or_node_));
        }

        ResortFront();
      }

      const auto new_i_raw = idx_[excluded_moves_];
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
  bool ResolveDoubleCountIfBranchRoot(BranchRootEdge edge) {
    if (edge.branch_root_key_hand_pair == key_hand_pair_) {
      sum_mask_.Reset(idx_.front());
      for (const auto i_raw : Skip(idx_, excluded_moves_ + 1)) {
        const auto& query = queries_[i_raw];
        const auto& child_key_hand_pair = query.GetBoardKeyHandPair();
        if (child_key_hand_pair == edge.child_key_hand_pair) {
          if (sum_mask_.Test(i_raw)) {
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
  /// 最善の子の探索結果を取得する
  const SearchResult& FrontResult() const { return results_[idx_[excluded_moves_]]; }

  // <PnDn>
  /// Pn を計算する
  constexpr PnDn GetPn() const {
    if (or_node_) {
      return GetPhi();
    } else {
      return GetDelta();
    }
  }

  /// Dn を計算する
  constexpr PnDn GetDn() const {
    if (or_node_) {
      return GetDelta();
    } else {
      return GetPhi();
    }
  }

  /// phi 値を計算する
  constexpr PnDn GetPhi() const {
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
  constexpr PnDn GetDelta() const {
    if (idx_.empty()) {
      return 0;
    }

    const auto& best_result = FrontResult();
    // 差分計算用の値を予め持っているので、高速に計算できる
    auto sum_delta = sum_delta_except_best_;
    auto max_delta = max_delta_except_best_;
    if (sum_mask_[idx_[excluded_moves_]]) {
      sum_delta = ClampPnDn(sum_delta + best_result.Delta(or_node_));
    } else {
      max_delta = std::max(max_delta, best_result.Delta(or_node_));
    }

    // 後回しにしている子局面が存在する場合、その値をδ値に加算しないと局面を過大評価してしまう。
    //
    // 例） sfen +P5l2/4+S4/p1p+bpp1kp/6pgP/3n1n3/P2NP4/3P1NP2/2P2S3/3K3L1 b RGSL2Prb2gsl3p 159
    //      1筋の合駒を考える時、玉方が合駒を微妙に変えることで読みの深さを指数関数的に大きくできてしまう
    if (mp_.size() > idx_.size()) {
      // 後回しにしている手1つにつき 1/8 点減点する。小数点以下は切り捨てするが、計算結果が 1 を下回る場合のみ
      // 1 に切り上げる。
      sum_delta += std::max<std::size_t>((mp_.size() - idx_.size()) / 8, 1);
    }

    const auto raw_delta = ClampPnDn(sum_delta + max_delta);
    if (excluded_moves_ > 0 && raw_delta == 0) {
      return kInfinitePnDn;
    }
    return raw_delta;
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
    PnDn delta_except_best = sum_delta_except_best_;
    if (mp_.size() > idx_.size()) {
      delta_except_best += std::max<std::size_t>((mp_.size() - idx_.size()) / 8, 1);
    }

    if (sum_mask_[idx_[excluded_moves_]]) {
      delta_except_best = SaturatedAdd(delta_except_best, max_delta_except_best_);
    }

    // 計算の際はオーバーフローに注意
    if (thdelta >= delta_except_best) {
      return ClampPnDn(thdelta - delta_except_best);
    }

    return 0;
  }
  // </PnDn>

  /**
   * @brief δ値の一時変数 `sum_delta_except_best_`, `max_delta_except_best_` を計算し直す
   */
  constexpr void RecalcDelta() {
    sum_delta_except_best_ = 0;
    max_delta_except_best_ = 0;

    for (const auto& i_raw : Skip(idx_, excluded_moves_ + 1)) {
      const auto delta_i = results_[i_raw].Delta(or_node_);
      if (sum_mask_[i_raw]) {
        sum_delta_except_best_ = ClampPnDn(sum_delta_except_best_ + delta_i);
      } else {
        max_delta_except_best_ = std::max(max_delta_except_best_, delta_i);
      }
    }
  }

  /// 探索結果を取得する（詰み局面）
  SearchResult GetProvenResult(const Node& n) const {
    if (or_node_) {
      // excluded_moves_ に関係なく最も良い手がほしいので FrontResult() は使えない
      const auto& result = results_[idx_.front()];
      const auto best_move = mp_[idx_[0]];
      const auto proof_hand = BeforeHand(n.Pos(), best_move, result.GetFinalData().hand);
      const auto mate_len = std::min(result.Len() + 1, kDepthMaxMateLen);
      const auto amount = result.Amount() + mp_.size() - 1;

      return SearchResult::MakeFinal<true>(proof_hand, mate_len, amount);
    } else {
      // 子局面の証明駒の極小集合を計算する
      HandSet set{ProofHandTag{}};
      MateLen mate_len = kZeroMateLen;
      SearchAmount amount = 1;
      for (const auto i_raw : idx_) {
        const auto& result = results_[i_raw];

        set.Update(result.GetFinalData().hand);
        amount = std::max(amount, result.Amount());
        if (MateLen{result.Len()} + 1 > mate_len) {
          mate_len = std::min(MateLen{result.Len()} + 1, kDepthMaxMateLen);
        }
      }

      // amount の総和を取ると値が大きくなりすぎるので子の数だけ足す
      // なお、子の個数が空の場合があるので注意。
      amount += std::max<SearchAmount>(mp_.size(), 1) - 1;

      const auto proof_hand = set.Get(n.Pos());
      return SearchResult::MakeFinal<true>(proof_hand, mate_len, amount);
    }
  }

  /// 探索結果を取得する（不詰局面）
  SearchResult GetDisprovenResult(const Node& n) const {
    // children_ は千日手エントリが手前に来るようにソートされているので、以下のようにして千日手判定ができる
    if (!mp_.empty()) {
      // excluded_moves_ に関係なく最も良い手がほしいので FrontResult() は使えない
      if (const auto& result = results_[idx_.front()]; result.GetFinalData().IsRepetition()) {
        const auto depth = result.GetFinalData().repetition_start;
        const auto amount = result.Amount();
        if (depth < n.GetDepth()) {
          return SearchResult::MakeRepetition(n.OrHand(), len_, amount, depth);
        } else {
          return SearchResult::MakeFinal<false>(n.OrHand(), len_, amount);
        }
      }
    }

    // フツーの不詰
    if (or_node_) {
      // 子局面の反証駒の極大集合を計算する
      HandSet set{DisproofHandTag{}};
      MateLen mate_len = kDepthMaxMateLen;
      SearchAmount amount = 1;
      for (const auto i_raw : idx_) {
        const auto& result = results_[i_raw];
        const auto child_move = mp_[i_raw];

        set.Update(BeforeHand(n.Pos(), child_move, result.GetFinalData().hand));
        amount = std::max(amount, result.Amount());
        if (result.Len() + 1 < mate_len) {
          mate_len = result.Len() + 1;
        }
      }
      amount += std::max<SearchAmount>(mp_.size(), 1) - 1;
      const auto disproof_hand = set.Get(n.Pos());

      return SearchResult::MakeFinal<false>(disproof_hand, mate_len, amount);
    } else {
      const auto& result = FrontResult();
      auto disproof_hand = result.GetFinalData().hand;
      const auto mate_len = std::min(result.Len() + 1, kDepthMaxMateLen);
      const auto amount = result.Amount() + mp_.size() - 1;

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

  /// 探索結果を取得する（不明局面）
  SearchResult GetUnknownResult(const Node& /* n */) const {
    const auto& result = FrontResult();
    const SearchAmount amount = result.Amount() + mp_.size() - 1;

    const UnknownData unknown_data{false, sum_mask_};
    return SearchResult::MakeUnknown(GetPn(), GetDn(), len_, amount, unknown_data);
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

  const bool or_node_;                       ///< 現局面が OR node かどうか
  const MovePicker mp_;                      ///< 現局面の合法手
  const DelayedMoveList delayed_move_list_;  ///< 後回しにしている手のグラフ構造
  const MateLen len_;                        ///< 現局面における残り探索手数
  const BoardKeyHandPair key_hand_pair_;  ///< 現局面の盤面ハッシュ値と持ち駒。二重カウント対策で用いる。
  const std::uint32_t multi_pv_;  ///< 何個の手を探索するか

  /// 子の現在の評価値結果一覧
  std::array<SearchResult, kMaxCheckMovesPerNode> results_;
  /// 子のクエリ一覧。コンストラクト時に作ったクエリを使い回すことで高速化できる
  std::array<tt::Query, kMaxCheckMovesPerNode> queries_;

  /// 現局面の評価値が古い探索情報に基づくものかどうか。TCA の探索延長の判断に用いる。
  bool does_have_old_child_{false};

  PnDn sum_delta_except_best_;  ///< 和でδを計上する子のうち現局面を除いたもののδ値の和
  PnDn max_delta_except_best_;  ///< 最大値でδを計上する子のうち現局面を除いたもののδ値の最大値

  /// δ値を和で計算すべき子の一覧。ビットが立っている子は和、立っていない子は最大値で計上する。
  BitSet64 sum_mask_;
  /// 現在有効な生添字の一覧。「良さ順」で並んでいる。
  FixedSizeStack<std::uint32_t, kMaxCheckMovesPerNode> idx_;

  std::uint32_t excluded_moves_{0};  ///< いくつ勝ちになる手を見つけたか
};
}  // namespace komori

#endif  // KOMORI_LOCAL_EXPANSION_HPP_
