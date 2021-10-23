#ifndef KOMORING_HEIGHTS_HPP_
#define KOMORING_HEIGHTS_HPP_

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../types.h"
#include "transposition_table.hpp"
#include "ttentry.hpp"

namespace komori {
namespace internal {
/// 詰将棋の最大手数。ミクロコスモス（1525手詰）より十分大きな値を設定する
constexpr Depth kMaxNumMateMoves = 3000;
/// 1 Cluster ごとの entry 数。小さくすればするほど高速に動作するが、エントリが上書きされやすくなるために
/// 超手数の詰み探索に失敗する確率が上がる。
constexpr std::size_t kClusterSize = 128;

/// 局面に対する詰みの状態を現す列挙型
enum class PositionMateKind {
  kUnknown,   ///< 不明
  kProven,    ///< 詰み
  kDisproven  ///< 不詰
};

/// 探索中に子局面の情報を一時的に覚えておくための構造体
struct ChildNodeCache {
  LookUpQuery query;
  TTEntry* entry;
  Move move;
  PnDn min_n, sum_n;
  std::uint32_t generation;
  int value;
};

/**
 * @brief 次に探索すべき子ノードを選択する
 *
 * 詰将棋探索では、TranspositionTable の LookUp に最も時間がかかる。そのため、TTEntry をキャッシュして
 * 必要最低限の回数だけ LookUp を行うようにすることで高速化を図る。
 *
 * OrNode と AndNode でコードの共通化を図るために、pn/dn ではなく min_n/sum_n を用いる。
 *
 *     OrNode:  min_n=pn, sum_n=dn
 *     AndNode: min_n=dn, sum_n=pn
 *
 * このようにすることで、いずれの場合の子局面選択も「min_n が最小となる子局面を選択する」という処理に帰着できる。
 *
 * @note コンストラクタ時点で子局面の TTEntry が存在しない場合、エントリの作成は実際の探索まで遅延される。
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

  MoveSelector(const Position& n, TranspositionTable& tt, Depth depth, PnDn th_sum_n);

  /// 子局面の entry の再 LookUp を行い、現曲面の pn/dn を計算し直す
  void Update();

  bool empty() const;

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
  /// 現時点で最善の手に対する TTEntry を返す
  /// @note entry が nullptr のために関数内で LookUp する可能性があるので、const メンバ関数にはできない
  TTEntry* FrontTTEntry();
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
};
}  // namespace internal

/// df-pn探索の本体
class DfPnSearcher {
 public:
  DfPnSearcher() = default;
  DfPnSearcher(const DfPnSearcher&) = delete;
  DfPnSearcher(DfPnSearcher&&) = delete;
  DfPnSearcher& operator=(const DfPnSearcher&) = delete;
  DfPnSearcher& operator=(DfPnSearcher&&) = delete;
  ~DfPnSearcher() = default;

  /// 内部変数（tt 含む）を初期化する
  void Init();
  /// tt のサイズを変更する
  void Resize(std::uint64_t size_mb);
  /// 探索局面数上限を設定する
  void SetMaxSearchNode(std::uint64_t max_search_node) { max_search_node_ = max_search_node; }
  /// 探索深さ上限を設定する
  void SetMaxDepth(Depth max_depth) { max_depth_ = max_depth; }

  /// df-pn 探索本体。局面 n が詰むかを調べる
  bool Search(Position& n, std::atomic_bool& stop_flag);

  /// 局面 n が詰む場合、最善手を返す。詰まない場合は MOVE_NONE を返す。
  Move BestMove(const Position& n);
  /// 局面 n が詰む場合、最善応手列を返す。詰まない場合は {} を返す。
  std::vector<Move> BestMoves(const Position& n);

 private:
  /// SearchPV で使用する局面の探索情報を保存する構造体
  struct MateMove {
    internal::PositionMateKind kind{internal::PositionMateKind::kUnknown};
    Move move{Move::MOVE_NONE};
    Depth num_moves{internal::kMaxNumMateMoves};
    Key repetition_start{};
  };

  /**
   * @brief df-pn 探索の本体。pn と dn がいい感じに小さいノードから順に最良優先探索を行う。
   *
   * @tparam kOrNode OrNode（詰ます側）なら true、AndNode（詰まされる側）なら false
   * @param n 現局面
   * @param thpn pn のしきい値。n の探索中に pn がこの値以上になったら探索を打ち切る。
   * @param thpn dn のしきい値。n の探索中に dn がこの値以上になったら探索を打ち切る。
   * @param depth 探索深さ
   * @param parents root から現局面までで通過した局面の key の一覧。千日手判定に用いる。
   * @param TBD.
   * @param entry 現局面の TTEntry。引数として渡すことで LookUp 回数をへらすことができる。
   */
  template <bool kOrNode>
  void SearchImpl(Position& n,
                  PnDn thpn,
                  PnDn thdn,
                  Depth depth,
                  std::unordered_set<Key>& parents,
                  const LookUpQuery& query,
                  TTEntry* entry);

  /**
   * @brief 局面 n の最善応手を再帰的に探索する
   *
   * OrNode では最短に、AndNode では最長になるように tt_ へ保存された手を選択する。
   * メモ化再帰により探索を効率化する。memo をたどることで最善応手列（PV）を求めることができる。
   */
  template <bool kOrNode>
  MateMove SearchPv(std::unordered_map<Key, MateMove>& memo, Position& n, Depth depth);

  void PrintProgress(const Position& n, Depth depth) const;

  TranspositionTable tt_{};
  /// Selector を格納する領域。stack に積むと stackoverflow になりがちなため
  std::vector<internal::MoveSelector<true>> or_selectors_{};
  std::vector<internal::MoveSelector<false>> and_selectors_{};

  std::atomic_bool* stop_{nullptr};
  std::uint64_t searched_node_{};
  std::chrono::system_clock::time_point start_time_;
  Depth max_depth_{internal::kMaxNumMateMoves};
  std::uint64_t max_search_node_{std::numeric_limits<std::uint64_t>::max()};
};
}  // namespace komori

#endif  // KOMORING_HEIGHTS_HPP_
