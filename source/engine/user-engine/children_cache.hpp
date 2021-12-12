#ifndef CHILDREN_CACHE_HPP_
#define CHILDREN_CACHE_HPP_

#include "transposition_table.hpp"
#include "typedefs.hpp"

namespace komori {
// Forward Declaration
class Node;
class CommonEntry;

/**
 * @brief 子ノードの置換表索引結果をキャッシュして、次に展開すべきノードを選択するためのクラス
 */
class ChildrenCache {
 public:
  /**
   * @brief Construct a new Children Cache object
   *
   * @param tt  置換表
   * @param n   現在の局面
   * @param query 現局面の置換表クエリ。引数として渡すことで高速化をはかる。
   */
  template <bool kOrNode>
  ChildrenCache(TranspositionTable& tt, const Node& n, const LookUpQuery& query, NodeTag<kOrNode>);

  ChildrenCache() = delete;
  ChildrenCache(const ChildrenCache&) = delete;
  ChildrenCache(ChildrenCache&&) = default;  ///< vector で領域確保させるために必要
  ChildrenCache& operator=(const ChildrenCache&) = delete;
  ChildrenCache& operator=(ChildrenCache&&) = delete;
  ~ChildrenCache() = default;

  /**
   * @brief 子ノードの置換表の再 LookUp を行い、entry の内容を更新する
   *
   * @param entry 置換表エントリ
   * @param num_searched 探索局面数
   * @param update_max_rank 上位何個まで子局面の再LookUpを行うか。デフォルトでは上限無制限。
   * @return CommonEntry* 更新後の entry。GC や Proven/Disproven の影響で entry とは指し先が異なる可能性がある
   */
  CommonEntry* Update(CommonEntry* entry,
                      std::uint64_t num_searched,
                      std::size_t update_max_rank = std::numeric_limits<std::size_t>::max());

  /// 現在の最善手を返す。合法手が 1 つ以上する場合に限り呼び出すことができる
  Move BestMove() const { return children_[idx_[0]].move; }
  /// BestMove() により探索をすすめるとき、子局面で用いる pn/dn の探索しきい値を求める
  std::pair<PnDn, PnDn> ChildThreshold(PnDn thpn, PnDn thdn) const;

  /// 現在の最善手に対する LookUpQuery を返す
  const LookUpQuery& BestMoveQuery() const { return children_[idx_[0]].query; }
  /// 現在の最善手に対する CommonEntry を返す。const メンバ関数でないのは、エントリが存在しなかった場合に
  /// 新規作成を行う必要があるため。
  CommonEntry* BestMoveEntry();
  /// UnprovenOldChild（詰みでも不詰でもない浅い深さの探索結果を参照しているノード）が存在すれば true
  bool DoesHaveOldChild() const { return does_have_old_child_; }

 private:
  struct NodeCache {
    LookUpQuery query;
    CommonEntry* entry;
    Move move;
    PnDn pn, dn;
    StateGeneration s_gen;
    int value;
  };

  /// 現局面が詰みであると報告する
  CommonEntry* SetProven(CommonEntry* entry, std::uint64_t num_searched);
  /// 現局面が不詰であると報告する
  CommonEntry* SetDisproven(CommonEntry* entry, std::uint64_t num_searched);
  /// 現局面の pn/dn を更新する。詰みでも不詰でもない場合に限り呼び出すことができる。
  CommonEntry* UpdateUnknown(CommonEntry* entry, std::uint64_t num_searched);
  /// 現在の最善手に対する攻め方の持ち駒を返す。子局面の ProperHand() から逆算して求めるため、
  /// n_->OrHand() とは異なる可能性がある。
  Hand BestMoveHand() const;

  /// 現在の次良手局面おけるφ値（OrNodeならpn、AndNodeならdn）を返す。
  /// 合法手が 1 手しかない場合、∞を返す。
  PnDn SecondPhi() const;
  /// 現局面におけるδ値（OrNodeならdn、AndNodeならpn）から最善手におけるφ値を引いた値を返す。
  PnDn DeltaExceptBestMove() const;

  /// NodeCache同士の比較演算子。sortしたときにφ値の昇順かつ千日手の判定がしやすい順番に並び替える。
  bool Compare(const NodeCache& lhs, const NodeCache& rhs) const;

  /// 現局面
  const Node& n_;
  /// 現局面における置換表クエリ
  const LookUpQuery& query_;
  const bool or_node_;
  bool does_have_old_child_{false};

  std::array<NodeCache, kMaxCheckMovesPerNode> children_;
  std::array<std::uint32_t, kMaxCheckMovesPerNode> idx_;
  std::size_t children_len_{0};
  PnDn delta_{0};
};
}  // namespace komori

#endif  // CHILDREN_CACHE_HPP_