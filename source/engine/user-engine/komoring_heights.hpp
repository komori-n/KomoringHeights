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

namespace komori {
namespace internal {

/// pn/dn を表す型
using PnDn = std::uint64_t;
/// 詰将棋の最大手数。ミクロコスモス（1525手詰）より十分大きな値を設定する
constexpr Depth kMaxNumMateMoves = 3000;
/// 1 Cluster ごとの entry 数。小さくすればするほど高速に動作するが、エントリが上書きされやすくなるために
/// 超手数の詰み探索に失敗する確率が上がる。
constexpr std::size_t kClusterSize = 128;
/// 1局面の最大王手/王手回避の着手数
constexpr std::size_t kMaxCheckMovesPerNode = 100;

/// 局面に対する詰みの状態を現す列挙型
enum class PositionMateKind {
  kUnknown,   ///< 不明
  kProven,    ///< 詰み
  kDisproven  ///< 不詰
};

/// Position の探索情報を格納するための構造体
struct TTEntry {
  std::uint32_t hash_high;  ///< board_keyの上位32bit
  Hand hand;                ///< 攻め方のhand。pn==0なら証明駒、dn==0なら反証駒を表す。
  PnDn pn, dn;              ///< pn, dn
  Depth depth;  ///< 探索深さ。千日手回避のためにdepthが違う局面は別局面として扱う
  std::uint32_t generation;  ///< 探索世代。古いものから順に上書きされる
};

/// TTEntryのカタマリ。第1要素に使用中のentry数が格納されている（二分探索の高速化のため）
/// cluster 内は hash_high の昇順でエントリを格納する。このようにすることで、LookUp 時に二分探索が使えるようになる。
using TTCluster = std::pair<std::size_t, std::array<TTEntry, kClusterSize>>;
using ClusterIterator = typename TTCluster::second_type::iterator;

class TranspositionTable {
 public:
  TranspositionTable() = default;
  TranspositionTable(const TranspositionTable&) = delete;
  TranspositionTable(TranspositionTable&&) = delete;
  TranspositionTable& operator=(const TranspositionTable&) = delete;
  TranspositionTable& operator=(TranspositionTable&&) = delete;
  ~TranspositionTable() = default;

  /// ハッシュサイズを hash_size_mb へ変更する
  void Resize(std::uint64_t hash_size_mb);
  /// 古い内容を削除して新たな探索が始まることを伝える
  void NewSearch();

  // <LookUp-Related>
  /**
   * @brief 局面 n に最も合致する TTEntry を返す
   *
   * @tparam kOrNode
   * @tparam kCreateIfNotExist true: 該当エントリが存在しない場合、エントリを新規作成する
   *                           false: 該当エントリが存在しない場合、エントリを作成しない（一時領域に結果が格納される）
   * @return std::pair<bool, TTEntry*> first: エントリが見つかったら true。kCreateIfNotExist==falseのとき、この値が
   *                                     falseならエントリは一時領域に格納されている（すなわち、
   *                                     次のLookUpまでに破棄すべき）ということを表している
   *                                   second:見つかったエントリへのポインタ
   *
   * @note
   * 内部では、局面 n の board_key, hand から LookUpImpl を呼び出している。
   * 制限事項や詳しい動作内容は LookUpImpl を参照。
   */
  template <bool kOrNode, bool kCreateIfNotExist>
  std::pair<bool, TTEntry*> LookUp(const Position& n, Depth depth);

  /**
   * @brief 局面 n で move を指した後の局面に最も合致する TTEntry を返す
   *
   * 内部では GetChildCluster して LookUpImpl を呼び出しているだけ。
   * 中間変数の cluster, hash_high, hand がいらない場合は GetChildCluster ではなくこちらを用いる。
   *
   * @tparam kCreateIfNotExist true: 該当エントリが存在しない場合、エントリを新規作成する
   *                           false: 該当エントリが存在しない場合、エントリを作成しない（一時領域に結果が格納される）
   * @return std::pair<bool, TTEntry*> first: エントリが見つかったら true。kCreateIfNotExist==falseのとき、この値が
   *                                     falseならエントリは一時領域に格納されている（すなわち、
   *                                     次のLookUpまでに破棄すべき）ということを表している
   *                                   second:見つかったエントリへのポインタ
   *
   * @caution
   * depth は move 後の局面（子局面）時点での手数を渡す。
   * 例）ルート局面の子局面の LookUpをしたい場合、
   * ```
   *     auto [found, entry] = tt_.LookUp<true>(n, 1);
   * ```
   * のように用いる。
   */
  template <bool kOrNode, bool kCreateIfNotExist>
  std::pair<bool, TTEntry*> LookUpChild(const Position& n, Move move, Depth depth);

  /**
   * @brief (hash_high, hand, depth) に最も合致する TTEntry を返す
   *
   * LookUp() の内部で使用している関数。
   * 子局面の TTEntry を高速に取得するために public にしている。（GetChildCluster と組み合わせて使う）
   *
   * cluster 内で、渡された局面に最もふさわしいエントリを探して返す。存在しない場合は新規作成される。
   * もし cluster が full だった場合、generation が最も古い局面へ上書きする。
   *
   * @return std::pair<bool, TTEntry*> first: エントリが見つかったら true。kCreateIfNotExist==falseのとき、この値が
   *                                     falseならエントリは一時領域に格納されている（すなわち、
   *                                     次のLookUpまでに破棄すべき）ということを表している
   *                                   second:見つかったエントリへのポインタ
   *
   * @caution
   * この関数の呼び出し前後で、同じクラスタの TTEntry の移動や削除が行われる可能性がある。
   * 他 entry の LookUp を行った場合、以前の entry がまだ有効かどうかは ValidateEntry でチェックする必要がある。
   */
  template <bool kCreateIfNotExist>
  std::pair<bool, TTEntry*> LookUpImpl(TTCluster& cluster, std::uint32_t hash_high, Hand hand, Depth depth);

  /**
   * @brief 局面 n で move を指した後の局面における cluster, hand, hash_high を返す。
   *
   * do_move と LookUp を組み合わせて LookUp するよりも高速に cluster や hand を求めることができる。
   */
  template <bool kOrNode>
  void GetChildCluster(const Position& n, Move move, TTCluster*& cluster, std::uint32_t& hash_high, Hand& hand);
  // </LookUp-Related>

  /**
   * @brief entry は証明駒 hand で詰みだと報告する
   *
   * この関数は該当エントリの更新に加え、不要エントリ（entry の優等局面）の削除を行う。
   *
   * @caution
   * この関数の呼び出し前後で、entry 自身を含め同じクラスタの TTEntry の移動や削除が行われる可能性がある。
   * 他 entry の LookUp を行った場合、以前の entry がまだ有効かどうかは ValidateEntry でチェックする必要がある。
   */
  void SetProven(TTEntry& entry, Hand hand);
  /**
   * @brief entry は反証駒 hand で不詰だと報告する
   *
   * この関数は該当エントリに加え、不要エントリ（entry の劣等局面のエントリ）の削除を行う。
   *
   * @caution
   * この関数の呼び出し前後で、entry 自身を含め同じクラスタの TTEntry の移動や削除が行われる可能性がある。
   * 他 entry の LookUp を行った場合、以前の entry がまだ有効かどうかは ValidateEntry でチェックする必要がある。
   */
  void SetDisproven(TTEntry& entry, Hand hand);
  /**
   * @brief entry は千日手による不詰だと報告する
   *
   * SetDisproven とは異なり、反証駒の計算や他エントリの削除は行わない。
   */
  void SetRepetitionDisproven(TTEntry& entry);

  /// ValidateEntry(cluster, .., entry) の簡略版。cluster などのパラメータが不要な場合はこの関数を用いる
  template <bool kOrNode>
  bool ValidateEntry(const Position& n, Depth depth, const TTEntry& entry) const;
  /**
   * @brief entry はまだ有効かどうかを判定する
   *
   * 前回 LookUp 後に別のエントリの LookUp を行った場合、エントリの格納位置が変わっている可能性がある。
   * この関数は、再度 LookUp することなく当該エントリがまだ有効化どうかを調べる事ができる。
   *
   * @return true  entry はまだ有効
   * @return false entry は無効。再度 LookUp し直す必要がある
   */
  bool ValidateEntry(const TTCluster& cluster,
                     std::uint32_t hash_high,
                     Hand hand,
                     Depth depth,
                     const TTEntry& entry) const;

  int Hashfull() const;

 private:
  /// entry が属する cluster を返す
  TTCluster& GetClusterOfEntry(const TTEntry& entry) const;

  /// [begin, end) で hash_high 以上を満たす最初のエントリを返す
  ClusterIterator LowerBound(TTCluster& cluster, std::uint32_t hash_high) const;
  /// [begin, end) で hash_high より大きいを満たす最初のエントリを返す
  ClusterIterator UpperBound(ClusterIterator begin, ClusterIterator end, std::uint32_t hash_high) const;
  /// [begin, end) が正しくソートされた状態になっているか確認する（デバッグ用）
  bool IsSorted(ClusterIterator begin, ClusterIterator end) const;

  /// tt の実体。align をキャッシュラインに揃えるために、tt_ に直接構築せず別でメモリを管理する
  std::vector<uint8_t> tt_raw_{};
  /// tt の本体
  TTCluster* tt_{nullptr};
  std::uint64_t num_clusters_{0};
  std::uint64_t clusters_mask_{0};
  /// LookUpImplNoCreate() でエントリが見つからなかった時に返却するエントリの一時領域
  TTEntry dummy_entry_{};
};

/// 探索中に子局面の情報を一時的に覚えておくための構造体
struct ChildNodeCache {
  TTCluster* cluster;
  TTEntry* entry;
  std::uint32_t hash_high;
  Hand hand;
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
  using FirstSearchOutput = std::pair<internal::PositionMateKind, Hand>;

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
   * @param entry 現局面の TTEntry。引数として渡すことで LookUp 回数をへらすことができる。
   */
  template <bool kOrNode>
  void SearchImpl(Position& n,
                  internal::PnDn thpn,
                  internal::PnDn thdn,
                  Depth depth,
                  std::unordered_set<Key>& parents,
                  internal::TTEntry* entry);

  /**
   * @brief 局面 n で固定深さ depth の探索を行う
   *
   * 局面 n を初めて探索する際に呼ぶ。探索前に固定深さの探索をすることで、全体として少しだけ高速化される。
   */
  template <bool kOrNode>
  FirstSearchOutput FirstSearch(Position& n, Depth depth, Depth remain_depth);

  /**
   * @brief 局面 n の最善応手を再帰的に探索する
   *
   * OrNode では最短に、AndNode では最長になるように tt_ へ保存された手を選択する。
   * メモ化再帰により探索を効率化する。memo をたどることで最善応手列（PV）を求めることができる。
   */
  template <bool kOrNode>
  MateMove SearchPv(std::unordered_map<Key, MateMove>& memo, Position& n, Depth depth);

  void PrintProgress(const Position& n, Depth depth) const;

  internal::TranspositionTable tt_{};
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
