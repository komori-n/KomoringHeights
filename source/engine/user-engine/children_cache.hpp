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
  bool is_first;            ///< この子局面を初めて探索するなら true。
  std::uint64_t board_key;  ///< 子局面の board_key
  Hand hand;                ///< 子局面の or_hand

  PnDn Pn() const { return search_result.Pn(); }
  PnDn Dn() const { return search_result.Dn(); }
  PnDn Phi(bool or_node) const { return or_node ? search_result.Pn() : search_result.Dn(); }
  PnDn Delta(bool or_node) const { return or_node ? search_result.Dn() : search_result.Pn(); }
};

/// 局面と子局面のハッシュ値および子局面のpn/dnをまとめた構造体
struct Edge;
}  // namespace detail

// Forward Declaration
class Node;

/**
 * @brief 子ノードの置換表索引結果をキャッシュして、次に展開すべきノードを選択するためのクラス
 *
 * 本クラスには大きく4つの役割がある。
 *
 * 1. n における合法手の一覧を記憶する
 * 2. 子局面に対する LookUp と置換表登録を行う
 * 3. LookUp 結果をよさげ順に並べる
 * 4. 現局面の pn/dn および証明駒／反証駒を計算する
 *
 * @note
 *
 * 以下に実装上の工夫、注意点を挙げる。
 *
 * # n における合法手の一覧を記憶する
 *
 * 明らかな千日手や子局面の中で証明済みノードを見つけた時など、pn/dn に影響を与えない範囲で合法手生成を
 * 省略することがある。
 *
 * # 子局面に対する LookUp と置換表登録を行う
 *
 * プログラム全体で最も処理負荷が高いのが置換表の LookUp 処理である。そのため、LookUp ができるだけ高速に動作するように
 * いろんな変数をキャッシュしている。なお、本クラスの責務はあくまで「子局面のキャッシュ」であるため、
 * 自局面の置換表登録は行わない。もし必要な場合、CurrentResult() の結果をもとに ChildrenCache の使用者が登録する
 * 必要がある。
 *
 * 細かい工夫点については、ChildrenCache::Child のコメントも参照。
 *
 * # LookUp 結果をよさげ順に並べる
 *
 * Child の並び順は UpdateFront() の呼び出し前後で変化する可能性があるので注意。
 *
 * # 現局面の pn/dn および証明駒／反証駒を計算する
 *
 * 特になし。
 */
class ChildrenCache {
 public:
  /**
   * @brief 子局面一覧を作成し、子局面を pn/dn がよさげな順に並べ替えて初期化する
   */
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

  /// 現在の最善手を返す。合法手が 1 つ以上する場合に限り呼び出すことができる
  Move BestMove() const { return NthChild(0).move.move; }
  bool BestMoveIsFirstVisit() const { return NthChild(0).is_first; }
  BitSet64 BestMoveSumMask() const;
  /**
   * @brief 最善手（i=0）への置換表登録と Child の再ソートを行う。
   *
   * @param search_result 置換表へ登録する内容
   * @param move_count   探索局面数
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
  bool IsSumChild(std::size_t i) const { return sum_mask_.Test(idx_[i]); }
  /// UpdateFront のソートしない版
  void UpdateNthChildWithoutSort(std::size_t i, const SearchResult& search_result);

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
