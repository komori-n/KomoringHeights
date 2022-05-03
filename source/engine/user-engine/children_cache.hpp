#ifndef CHILDREN_CACHE_HPP_
#define CHILDREN_CACHE_HPP_

#include <array>
#include <utility>

#include "bitset.hpp"
#include "move_picker.hpp"
#include "transposition_table.hpp"
#include "typedefs.hpp"

namespace komori {
namespace detail {
/**
 * @brief 子局面の置換表 LookUp のキャッシュを行う構造体。
 */
struct Child {
  std::uint64_t board_key;  ///< 子局面の board_key
  Hand hand;                ///< 子局面の or_hand
  std::uint32_t next_dep;   ///< 次に展開すべき要素の i_raw + 1。0 は無効値。
};

class IndexTable {
 public:
  auto Push(std::uint32_t i_raw) noexcept {
    KOMORI_PRECONDITION(len_ < kMaxCheckMovesPerNode);

    const auto i = len_++;
    data_[i] = i_raw;
    return i;
  }

  void Pop() noexcept {
    KOMORI_PRECONDITION(len_ > 0);
    --len_;
  }

  auto operator[](std::uint32_t i) const noexcept {
    KOMORI_PRECONDITION(i < len_);
    return data_[i];
  }

  auto begin() noexcept { return data_.begin(); }
  auto begin() const noexcept { return data_.begin(); }
  auto end() noexcept { return data_.begin() + len_; }
  auto end() const noexcept { return data_.begin() + len_; }
  auto size() const noexcept { return len_; }
  auto empty() const noexcept { return len_ == 0; }
  auto front() const noexcept {
    KOMORI_PRECONDITION(!empty());
    return data_[0];
  }

 private:
  std::array<std::uint32_t, kMaxCheckMovesPerNode> data_;
  std::uint32_t len_{0};
};

/// 局面と子局面のハッシュ値および子局面のpn/dnをまとめた構造体
struct Edge;
}  // namespace detail

// Forward Declaration
class Node;

/**
 * @brief 子局面の展開を制御するクラス
 */
class ChildrenCache {
 public:
  ChildrenCache(TranspositionTable& tt,
                Node& n,
                bool first_search,
                BitSet64 sum_mask = BitSet64::Full(),
                ChildrenCache* parent = nullptr);

  ChildrenCache() = delete;
  /// 計算コストがとても大きいので move でも禁止。例えば、v0.4.1 だと ChildrenCache は 16.5MB。
  ChildrenCache(const ChildrenCache&) = delete;
  ChildrenCache(ChildrenCache&&) = delete;
  ChildrenCache& operator=(const ChildrenCache&) = delete;
  ChildrenCache& operator=(ChildrenCache&&) = delete;
  ~ChildrenCache() = default;

  /// 現在の最善手
  Move BestMove() const { return mp_[idx_.front()].move; }

  /// 現在の最善手が初探索かどうか
  bool BestMoveIsFirstVisit() const { return results_[idx_.front()].IsFirstVisit(); }

  BitSet64 BestMoveSumMask() const;
  /**
   * @brief 最善手（i=0）への置換表登録と Child の再ソートを行う。
   */
  void UpdateBestChild(const SearchResult& search_result);

  /**
   * @brief 現在の pn/dn および証明駒／反証駒を返す。
   *
   * @param n  現局面。コンストラクタで渡した局面と同じである必要がある。
   * @return SearchResult 現局面の状況。
   */
  SearchResult CurrentResult(const Node& n) const;

  /// BestMove() により探索をすすめるとき、子局面で用いる pn/dn の探索しきい値を求める
  std::pair<PnDn, PnDn> ChildThreshold(PnDn thpn, PnDn thdn) const;
  /// UnprovenOldChild（詰みでも不詰でもない浅い深さの探索結果を参照しているノード）が存在すれば true
  bool DoesHaveOldChild() const { return does_have_old_child_; }

 private:
  /// i 番目に良い手に対する Child を返す
  detail::Child& NthChild(std::size_t i) { return children_[idx_[i]]; }

  const detail::Child& NthChild(std::size_t i) const { return children_[idx_[i]]; }

  /// UpdateFront のソートしない版
  bool UpdateNthChildWithoutSort(std::size_t i, const SearchResult& search_result);

  /// 子 i を展開する。n が必要なのは、pn/dn の初期値を計算するため
  void Expand(Node& n, std::size_t i, bool first_search);
  /// 子 i 以外がソートされていて i だけがソートされていない場合、i を適切な位置に挿入する
  void Refresh(std::size_t i);

  /// 現局面が詰みであることがわかっている時、その SearchResult を計算して返す
  SearchResult GetProvenResult(const Node& n) const;
  /// 現局面が不詰であることがわかっている時、その SearchResult を計算して返す
  SearchResult GetDisprovenResult(const Node& n) const;
  /// 現局面が詰みでも不詰でもないことがわかっている時、その SearchResult を計算して返す
  SearchResult GetUnknownResult(const Node& n) const;

  /// 現局面におけるδ値（OrNodeならdn、AndNodeならpn）から最善手におけるφ値を引いた値を返す。
  PnDn NewThdeltaForBestMove(PnDn thdelta) const;
  /// δ値を計算するために使用する内部変数（XXX_delta_except_best_）を計算し直す
  void RecalcDelta();
  /// 現在の pn
  PnDn GetPn() const;
  /// 現在の dn
  PnDn GetDn() const;
  /// 現在のφ値
  PnDn GetPhi() const;
  /// 現在のδ値
  PnDn GetDelta() const;
  /// 現在のδ値を sum_delta, max_delta に分解したもの。GetDelta() ではこの値をもとに現局面のδ値を計算する
  std::pair<PnDn, PnDn> GetRawDelta() const;
  /// 現在の次良手局面おけるφ値（OrNodeならpn、AndNodeならdn）を返す。合法手が 1 手しかない場合、∞を返す。
  PnDn GetSecondPhi() const;

  /// 子 i を終点とする二重カウントになっていたらそれを解消する。詳しいアルゴリズムは cpp のコメントを参照。
  void EliminateDoubleCount(TranspositionTable& tt, const Node& n, std::size_t i);
  /// edge を起点とする二重カウント状態を解消する。詳しくは EliminateDoubleCount() のコメントを参照。
  void SetBranchRootMaxFlag(const detail::Edge& edge, bool branch_root_is_or_node);

  auto MakeComparer() const {
    return [this](std::size_t i_raw, std::size_t j_raw) -> bool {
      const auto& left_result = results_[i_raw];
      const auto& right_result = results_[j_raw];

      if (left_result.Phi(or_node_) != right_result.Phi(or_node_)) {
        return left_result.Phi(or_node_) < right_result.Phi(or_node_);
      } else if (left_result.Delta(or_node_) != right_result.Delta(or_node_)) {
        return left_result.Delta(or_node_) > right_result.Delta(or_node_);
      }

      if (left_result.Dn() == 0 && right_result.Dn() == 0) {
        // DisprovenState と RepetitionState はちゃんと順番をつけなければならない
        // - repetition -> まだ頑張れば詰むかもしれない
        // - disproven -> どうやっても詰まない
        auto lstate = left_result.GetNodeState();
        auto rstate = right_result.GetNodeState();

        // or node -> repetition < disproven になってほしい（repetition なら別経路だと詰むかもしれない）
        // and node -> disproven < repetition になってほしい（disproven なら経路に関係なく詰まないから）
        // -> !or_node ^ (int)repetition < (int)disproven
        if (lstate != rstate) {
          return !or_node_ ^ (static_cast<int>(lstate) < static_cast<int>(rstate));
        }
      }

      return mp_[i_raw] < mp_[j_raw];
    };
  }

  /// 展開元の局面が OR node なら true
  /// コンストラクト時に渡された node から取得する。node 全部をコピーする必要がないので、これだけ持っておく。

  const bool or_node_;
  /// <各子局面の情報>
  /// 現局面の合法手一覧。コンストラクト時に全合法手を生成するので const にできる。
  const MovePicker mp_;
  std::array<detail::Child, kMaxCheckMovesPerNode> children_;
  std::array<LookUpQuery, kMaxCheckMovesPerNode> queries_;
  std::array<SearchResult, kMaxCheckMovesPerNode> results_;

  /// 和で Delta を計上する子局面の一覧
  /// max でなく sum のマスクを持つ理由は、合法手が 64 個以上の場合、set から溢れた手を max child として扱いたいため。
  BitSet64 sum_mask_;
  /// </各子局面の情報>

  /// mp_ を Phi 値の降順にソートした時のインデックス。データ自体を並べ替えていると時間がかかるので、ソートの際は idx_
  /// の更新のみ行う。
  ///
  /// idx_ 適用前のindexを表す変数は i、 適用後のindexを表す変数は i_raw をそれぞれ用いる。
  detail::IndexTable idx_{};

  /// 現局面が old child を持つなら true。
  /// ここで old child とは、置換表に登録されている深さが現局面のそれよりも深いような局面のことを言う。
  bool does_have_old_child_{false};

  // <delta>
  // これらの値を事前計算しておくことで、Delta値を O(1) で計算できる。詳しくは GetDelta() を参照。
  /// 和で計算する子局面のDelta値のうち、最善要素を除いたものの総和。
  PnDn sum_delta_except_best_;
  /// maxで計算する子局面のDelta値のうち、最善要素を除いたものの最大値。
  PnDn max_delta_except_best_;
  // </delta>

  // <double-count>
  // 二重カウント対策のために必要な値たち

  const std::uint64_t curr_board_key_;
  const Hand or_hand_;
  ChildrenCache* const parent_;
  // </double-count>
};
}  // namespace komori

#endif  // CHILDREN_CACHE_HPP_
