#ifndef CHILDREN_CACHE_HPP_
#define CHILDREN_CACHE_HPP_

#include <array>
#include <utility>

#include "bitset.hpp"
#include "transposition_table.hpp"
#include "typedefs.hpp"

namespace komori {
namespace detail {
/**
 * @brief 子局面の置換表 LookUp のキャッシュを行う構造体。
 */
struct Child {
  ExtMove move;  ///< 子局面への move とその簡易評価値

  LookUpQuery query;  ///< 子局面の置換表エントリを LookUp するためのクエリ
  SearchResult search_result;  ///< 子局面の現在の pn/dn の値。LookUp はとても時間がかかるので、前回の LookUp
                               ///< 結果をコピーして持っておく。
  std::uint64_t board_key;  ///< 子局面の board_key
  Hand hand;                ///< 子局面の or_hand
  std::size_t next_dep;     ///< 次に展開すべき要素の i_raw + 1。0 は無効値。

  PnDn Pn() const { return search_result.Pn(); }
  PnDn Dn() const { return search_result.Dn(); }
  PnDn Phi(bool or_node) const { return or_node ? search_result.Pn() : search_result.Dn(); }
  PnDn Delta(bool or_node) const { return or_node ? search_result.Dn() : search_result.Pn(); }
  bool IsFirstVisit() const { return search_result.IsFirstVisit(); }

  void LookUp(Node& n);
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
  Move BestMove() const {
    KOMORI_PRECONDITION(effective_len_ > 0);
    return NthChild(0).move.move;
  }

  /// 現在の最善手が初探索かどうか
  bool BestMoveIsFirstVisit() const {
    KOMORI_PRECONDITION(effective_len_ > 0);
    return NthChild(0).IsFirstVisit();
  }

  BitSet64 BestMoveSumMask() const {
    auto& best_child = NthChild(0);
    KOMORI_PRECONDITION(IsNotFinal(best_child.search_result.GetNodeState()));

    if (auto unknown = best_child.search_result.TryGetUnknown()) {
      // secret には BitSet のビットを反転した値が格納されているので注意
      return BitSet64{~unknown->Secret()};
    }

    UNREACHABLE
  }
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
  detail::Child& NthChild(std::size_t i) {
    KOMORI_PRECONDITION(i < effective_len_);
    return children_[idx_[i]];
  }

  const detail::Child& NthChild(std::size_t i) const {
    KOMORI_PRECONDITION(i < effective_len_);
    return children_[idx_[i]];
  }

  bool IsSumChild(std::size_t i) const {
    KOMORI_PRECONDITION(i < effective_len_);
    return sum_mask_.Test(idx_[i]);
  }

  auto PushEffectiveChild(std::size_t i_raw) {
    KOMORI_PRECONDITION(i_raw < actual_len_);
    const auto i = effective_len_++;
    idx_[i] = i_raw;

    return i;
  }

  void PopEffectiveChild() {
    KOMORI_PRECONDITION(effective_len_ > 0);
    effective_len_--;
  }

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

  /// 展開元の局面が OR node なら true
  /// コンストラクト時に渡された node から取得する。node 全部をコピーする必要がないので、これだけ持っておく。
  const bool or_node_;
  /// 現局面が old child を持つなら true。
  /// ここで old child とは、置換表に登録されている深さが現局面のそれよりも深いような局面のことを言う。
  bool does_have_old_child_{false};

  /// 子局面とその pn/dn 値等の情報一覧。MovePicker の手と同じ順番で格納されている
  std::array<detail::Child, kMaxCheckMovesPerNode> children_;
  /// children_ を良さげ順にソートした時のインデックス。children_ 自体を move(copy) すると時間がかかるので、
  /// ソートの際は idx_ だけ並び替えを行う。
  std::array<std::uint32_t, kMaxCheckMovesPerNode> idx_;
  /// 和で Delta を計上する子局面の一覧
  /// max でなく sum のマスクを持つ理由は、合法手が 64 個以上の場合、set から溢れた手を max child として扱いたいため。
  BitSet64 sum_mask_;
  /// 子局面の数。
  std::size_t effective_len_{0};
  std::size_t actual_len_{0};

  // <delta>
  // これらの値を事前計算しておくことで、Delta値を O(1) で計算できる。詳しくは GetDelta() を参照。
  /// 和で計算する子局面のDelta値のうち、最善要素を除いたものの総和。
  PnDn sum_delta_except_best_;
  /// maxで計算する子局面のDelta値のうち、最善要素を除いたものの最大値。
  PnDn max_delta_except_best_;
  // </delta>

  int max_node_num_{0};

  // <double-count>
  // 二重カウント対策のために必要な値たち

  std::uint64_t curr_board_key_;
  Hand or_hand_;
  ChildrenCache* parent_;
  // </double-count>
};
}  // namespace komori

#endif  // CHILDREN_CACHE_HPP_
