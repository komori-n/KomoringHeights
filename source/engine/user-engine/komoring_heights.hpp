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
#include "children_cache.hpp"
#include "move_picker.hpp"
#include "node.hpp"
#include "transposition_table.hpp"
#include "ttcluster.hpp"
#include "usi.hpp"

namespace komori {
// forward decleration
class NodeHistory;

class SearchProgress {
 public:
  void NewSearch();
  void Visit(Depth depth) {
    depth_ = std::max(depth_, depth);
    node_++;
  }
  auto NodeCount() const { return node_; }
  bool IsEnd(std::uint64_t max_search_node) const { return node_ >= max_search_node; }

  void WriteTo(UsiInfo& output) const;

 private:
  std::chrono::system_clock::time_point start_time_;
  Depth depth_;
  std::uint64_t node_;
};

/// df-pn探索の本体
class KomoringHeights {
 public:
  KomoringHeights() = default;
  KomoringHeights(const KomoringHeights&) = delete;
  KomoringHeights(KomoringHeights&&) = delete;
  KomoringHeights& operator=(const KomoringHeights&) = delete;
  KomoringHeights& operator=(KomoringHeights&&) = delete;
  ~KomoringHeights() = default;

  /// 内部変数（tt 含む）を初期化する
  void Init();
  /// tt のサイズを変更する
  void Resize(std::uint64_t size_mb);
  /// 探索局面数上限を設定する
  void SetMaxSearchNode(std::uint64_t max_search_node) { max_search_node_ = max_search_node; }
  /// 探索深さ上限を設定する
  void SetMaxDepth(Depth max_depth) { max_depth_ = max_depth; }
  /// 余詰探索
  void SetExtraSearchCount(int extra_search_count) { extra_search_count_ = extra_search_count; }

  /// 探索情報のPrintを指示する。Printが完了したらフラグはfalseになる
  void SetPrintFlag() { print_flag_ = true; }

  /// df-pn 探索本体。局面 n が詰むかを調べる
  bool Search(Position& n, std::atomic_bool& stop_flag);

  /// 局面 n が詰む場合、最善応手列を返す。詰まない場合は {} を返す。
  std::vector<Move> BestMoves(Position& n);

  void ShowValues(Position& n, const std::vector<Move>& moves);

  UsiInfo Info() const;
  void PrintDebugInfo() const;

 private:
  static inline constexpr Depth kNonRepetitionDepth = Depth{kMaxNumMateMoves + 1};
  static inline constexpr Depth kNoMateLen = Depth{-1};

  struct NumMoves {
    int num{kNoMateLen};  ///< 詰み手数
    int surplus{0};       ///< 駒余りの枚数
  };

  struct MateMoveCache {
    Move move{MOVE_NONE};
    NumMoves num_moves{};
  };

  /**
   * @brief df-pn 探索の本体。pn と dn がいい感じに小さいノードから順に最良優先探索を行う。
   *
   * @tparam kOrNode OrNode（詰ます側）なら true、AndNode（詰まされる側）なら false
   * @param n 現局面
   * @param thpn pn のしきい値。n の探索中に pn がこの値以上になったら探索を打ち切る。
   * @param thpn dn のしきい値。n の探索中に dn がこの値以上になったら探索を打ち切る。
   * @param query 現局面の置換表クエリ。引数として渡すことで高速化をはかる。
   * @param entry 現局面の CommonEntry。引数として渡すことで LookUp 回数をへらすことができる。
   * @param inc_flag infinite loopが懸念されるときはtrue。探索を延長する。
   */
  template <bool kOrNode>
  void SearchImpl(Node& n, PnDn thpn, PnDn thdn, const LookUpQuery& query, CommonEntry* entry, bool inc_flag);

  /**
   * @brief 探索深さ remain_depth で静止探索を行う
   *
   * @tparam kOrNode OrNode（詰ます側）なら true、AndNode（詰まされる側）なら false
   * @param n 現局面
   * @param remain_depth 残り探索深さ
   * @param query 現局面の置換表クエリ。引数として渡すことで高速化をはかる。
   */
  template <bool kOrNode>
  void SearchLeaf(Node& n, Depth remain_depth, const LookUpQuery& query);

  /**
   * @brief Pv（最善応手列）を再帰的に探索する
   *
   * @param mate_table      探索結果のメモ
   * @param search_history  現在探索中の局面
   * @param n               現局面
   * @return std::pair<NodeCache, Depth>
   *     first   局面の探索結果
   *     second  firstが千日手絡みの評価値の場合、千日手がスタートした局面の深さ。
   *             firstが千日手絡みの評価値ではない場合、kNonRepetitionDepth。
   */
  template <bool kOrNode>
  std::pair<NumMoves, Depth> MateMovesSearchImpl(std::unordered_map<Key, MateMoveCache>& mate_table,
                                                 std::unordered_map<Key, Depth>& search_history,
                                                 Node& n);

  /**
   * @brief 局面 n が best_moves により詰みのとき、別のより短い詰み手順がないかどうかを調べる
   *
   * @param n 現局面
   * @param best_moves 現在のPV（最善応手列）
   */
  bool ExtraSearch(Node& n, std::vector<Move> best_moves);

  void PrintProgress(const Node& n) const;

  TranspositionTable tt_{};
  std::stack<MovePicker> pickers_{};
  std::stack<ChildrenCache> children_cache_{};

  std::atomic_bool* stop_{nullptr};
  std::atomic_bool print_flag_{false};
  SearchProgress progress_{};
  Score score_{};
  Depth max_depth_{kMaxNumMateMoves};
  int extra_search_count_{0};
  std::uint64_t max_search_node_{std::numeric_limits<std::uint64_t>::max()};
};
}  // namespace komori

#endif  // KOMORING_HEIGHTS_HPP_
