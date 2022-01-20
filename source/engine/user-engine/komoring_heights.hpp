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
#include "pv_tree.hpp"
#include "transposition_table.hpp"
#include "usi.hpp"

namespace komori {
// forward decleration
class NodeHistory;

namespace detail {
class SearchProgress {
 public:
  void NewSearch();
  void Visit(Depth depth, std::uint64_t move_count) {
    depth_ = std::max(depth_, depth);
    move_count_ = move_count;
  }

  void WriteTo(UsiInfo& output) const;
  auto MoveCount() const { return move_count_; }

 private:
  std::chrono::system_clock::time_point start_time_;
  Depth depth_;
  std::uint64_t move_count_;
};
}  // namespace detail

/// df-pn探索の本体
class KomoringHeights {
 public:
  KomoringHeights();
  KomoringHeights(const KomoringHeights&) = delete;
  KomoringHeights(KomoringHeights&&) = delete;
  KomoringHeights& operator=(const KomoringHeights&) = delete;
  KomoringHeights& operator=(KomoringHeights&&) = delete;
  ~KomoringHeights() = default;

  /// 内部変数（tt 含む）を初期化する
  void Init(std::uint64_t size_mb);
  /// 探索局面数上限を設定する
  void SetMaxSearchNode(std::uint64_t max_search_node) { max_search_node_ = max_search_node; }
  /// 探索深さ上限を設定する
  void SetMaxDepth(Depth max_depth) { max_depth_ = max_depth; }
  /// 余詰探索で探索する局面数
  void SetYozumeCount(int yozume_count) { yozume_node_count_ = yozume_count; }
  /// 余詰探索で何個まで別解を探索するか
  void SetYozumePath(int yozume_path) { yozume_search_count_ = yozume_path; }
  /// 詰将棋探索をやめさせる
  void SetStop() { stop_ = true; }
  /// stopフラグをクリアする
  void ResetStop() { stop_ = false; }

  /// 探索情報のPrintを指示する。Printが完了したらフラグはfalseになる
  void SetPrintFlag() { print_flag_ = true; }

  /// df-pn 探索本体。局面 n が詰むかを調べる
  NodeState Search(Position& n, bool is_root_or_node);
  /// 見つけた詰み手順を返す
  const auto& BestMoves() const { return best_moves_; }

  void ShowValues(Position& n, bool is_root_or_node, const std::vector<Move>& moves);
  void ShowPv(Position& n, bool is_root_or_node);

  UsiInfo Info() const;

 private:
  /**
   * @brief
   *
   * @param n         現局面
   * @param thpn      pn のしきい値
   * @param thdn      dn のしきい値
   * @param cache     n の子局面の LookUp 結果のキャッシュ
   * @param inc_flag  探索延長フラグ。true なら thpn/thdn を 1 回だけ無視して子局面を展開する
   * @return SearchResult  n の探索結果。この時点では tt_ にはに登録されていないので、
   *                            必要なら呼び出し側が登録する必要がある
   */
  SearchResult SearchImpl(Node& n, PnDn thpn, PnDn thdn, ChildrenCache& cache, bool inc_flag);

  MateLen PvSearch(Node& n, MateLen alpha, MateLen beta);

  /// CommonEntry に保存された best_move を元に最善応手列（PV）を復元する
  std::vector<Move> GetPv(Node& n);

  void PrintProgress(const Node& n) const;

  bool IsSearchStop() const;

  TranspositionTable tt_;
  std::stack<ChildrenCache> children_cache_{};
  std::stack<MovePicker> pickers_{};

  std::atomic_bool stop_{false};
  Timer gc_timer_{};
  TimePoint last_gc_{};

  std::atomic_bool print_flag_{false};
  detail::SearchProgress progress_{};
  Score score_{};
  Depth max_depth_{kMaxNumMateMoves};
  std::uint64_t max_search_node_{std::numeric_limits<std::uint64_t>::max()};

  /// 最善応手列（PV）の結果。CalcBestMoves() がそこそこ重いので、ここに保存しておく。
  std::vector<Move> best_moves_{};
  PvTree pv_tree_{};
  std::uint64_t yozume_node_count_{};
  std::uint64_t yozume_search_count_{};
};
}  // namespace komori

#endif  // KOMORING_HEIGHTS_HPP_
