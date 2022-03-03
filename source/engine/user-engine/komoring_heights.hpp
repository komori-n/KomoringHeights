#ifndef KOMORING_HEIGHTS_HPP_
#define KOMORING_HEIGHTS_HPP_

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../thread.h"
#include "../../types.h"
#include "children_cache.hpp"
#include "engine_option.hpp"
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
  void NewSearch(std::uint64_t max_num_moves, Thread* thread);
  void Visit(Depth depth) { depth_ = std::max(depth_, depth); }

  UsiInfo GetInfo() const;
  std::uint64_t MoveCount() const { return thread_->nodes; }
  bool IsStop() const { return MoveCount() >= max_num_moves_; }

  void StartExtraSearch(std::uint64_t yozume_count) {
    max_num_moves_backup_ = max_num_moves_;
    max_num_moves_ = std::min(max_num_moves_, MoveCount() + yozume_count);
  }

  void EndYozumeSearch() { max_num_moves_ = max_num_moves_backup_; }

 private:
  std::chrono::system_clock::time_point start_time_;
  Depth depth_;
  std::uint64_t max_num_moves_;
  std::uint64_t max_num_moves_backup_;
  Thread* thread_;
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
  void Init(EngineOption option, Thread* thread);
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
  SearchResult PostSearchEntry(Node& n, Move move);
  SearchResult UselessDropSearchEntry(Node& n, Move move);
  SearchResult SearchEntry(Node& n, PnDn thpn = kInfinitePnDn, PnDn thdn = kInfinitePnDn);
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

  MateLen PostSearch(Node& n, MateLen alpha, MateLen beta);
  std::vector<Move> TraceBestMove(Node& n);
  void PrintYozume(Node& n, const std::vector<Move>& pv);

  /// CommonEntry に保存された best_move を元に最善応手列（PV）を復元する
  std::vector<Move> GetPv(Node& n);

  void PrintProgress(const Node& n) const;

  bool IsSearchStop() const;

  TranspositionTable tt_;
  Thread* thread_{nullptr};
  std::stack<ChildrenCache> children_cache_{};
  std::stack<MovePicker> pickers_{};

  std::atomic_bool stop_{false};
  std::uint64_t next_gc_count_{0};

  std::atomic_bool print_flag_{false};
  detail::SearchProgress progress_{};
  Score score_{};

  /// 最善応手列（PV）の結果。CalcBestMoves() がそこそこ重いので、ここに保存しておく。
  std::vector<Move> best_moves_{};
  PvTree pv_tree_{};
  EngineOption option_;
};
}  // namespace komori

#endif  // KOMORING_HEIGHTS_HPP_
