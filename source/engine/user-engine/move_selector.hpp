#ifndef MOVE_SELECTOR_HPP_
#define MOVE_SELECTOR_HPP_

#include <array>
#include <unordered_set>

#include "../../types.h"
#include "transposition_table.hpp"
#include "ttcluster.hpp"

namespace komori {
/// 探索中に子局面の情報を一時的に覚えておくための構造体
struct ChildNodeCache {
  LookUpQuery query;
  CommonEntry* entry;
  Move move;
  PnDn min_n, sum_n;
  StateGeneration s_gen;
  int value;
};

/**
 * @brief 次に探索すべき子ノードを選択する
 *
 * 詰将棋探索では、TranspositionTable の LookUp に最も時間がかかる。そのため、CommonEntry をキャッシュして
 * 必要最低限の回数だけ LookUp を行うようにすることで高速化を図る。
 *
 * OrNode と AndNode でコードの共通化を図るために、pn/dn ではなく min_n/sum_n を用いる。
 *
 *     OrNode:  min_n=pn, sum_n=dn
 *     AndNode: min_n=dn, sum_n=pn
 *
 * このようにすることで、いずれの場合の子局面選択も「min_n が最小となる子局面を選択する」という処理に帰着できる。
 *
 * @note コンストラクタ時点で子局面の CommonEntry が存在しない場合、エントリの作成は実際の探索まで遅延される。
 * その場合 ChildNodeCache::entry が nullptr になる可能性がある。
 */
template <bool kOrNode>
class MoveSelector {
 public:
  MoveSelector() = delete;
  MoveSelector(const MoveSelector&) = delete;
  MoveSelector(MoveSelector&&) noexcept = default;  ///< vector で領域確保させるために必要
  MoveSelector& operator=(const MoveSelector&) = delete;
  MoveSelector& operator=(MoveSelector&&) = delete;
  ~MoveSelector() = default;

  MoveSelector(const Position& n,
               TranspositionTable& tt,
               const std::unordered_set<Key>& parents,
               Depth depth,
               Key path_key);

  /// 子局面の entry の再 LookUp を行い、現曲面の pn/dn を計算し直す
  void Update();

  PnDn Pn() const;
  PnDn Dn() const;
  /// 現局面は千日手による不詰か
  bool IsRepetitionDisproven() const;

  /// 現局面に対する反証駒を計算する
  Hand ProofHand() const;
  /// 現局面に対する反証駒を計算する
  Hand DisproofHand() const;

  /// 現時点で最善の手を返す
  Move FrontMove() const;
  /// 現時点で最善の手に対する CommonEntry を返す
  /// @note entry が nullptr のために関数内で LookUp する可能性があるので、const メンバ関数にはできない
  CommonEntry* FrontEntry();
  /// 現時点で最善の手に対する LookUpQuery を返す
  const LookUpQuery& FrontLookUpQuery() const;

  /**
   * @brief 子局面の探索の (pn, dn) のしきい値を計算する
   *
   * @param thpn 現局面の pn のしきい値
   * @param thdn 現曲面の dn のしきい値
   * @return std::pair<PnDn, PnDn> 子局面の (pn, dn) のしきい値の組
   */
  std::pair<PnDn, PnDn> ChildThreshold(PnDn thpn, PnDn thdn) const;

  bool DoesHaveOldChild() const { return does_have_old_child_; }

 private:
  /**
   * @brief 子局面同士を探索したい順に並べ替えるための比較演算子
   *
   * 以下の順で判定を行いエントリを並べ替える
   *   1. min_n が小さい順
   *   2. generation が小さい順（最も昔に探索したものから順）
   *   3. move.value が小さい順
   */
  bool Compare(const ChildNodeCache& lhs, const ChildNodeCache& rhs) const;

  /// 現局面の min_n を返す
  PnDn MinN() const;
  /// 現局面の sum_n を返す
  PnDn SumN() const;
  /// 子局面のうち 2 番目に小さい min_n を返す
  PnDn SecondMinN() const;
  /// children_[0] を除いた sum_n を返す
  PnDn SumNExceptFront() const;
  /// 現時点で最善の手の持ち駒を返す
  /// @note 詰み／不詰の局面では証明駒／反証駒を返す（現局面の持ち駒とは一致しない可能性がある）
  Hand FrontHand() const;

  const Position& n_;
  TranspositionTable& tt_;
  const Depth depth_;

  std::array<ChildNodeCache, kMaxCheckMovesPerNode> children_;
  std::size_t children_len_;
  PnDn sum_n_;
  bool does_have_old_child_;
};
}  // namespace komori

#endif  // MOVE_SELECTOR_HPP_