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
  /// 見つけた詰み手順を返す
  const auto& BestMoves() const { return best_moves_; }

  void ShowValues(Position& n, const std::vector<Move>& moves);
  void ShowPv(Position& n);

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
   * @brief
   *
   * @tparam kOrNode
   * @param n         現局面
   * @param thpn      pn のしきい値
   * @param thdn      dn のしきい値
   * @param cache     n の子局面の LookUp 結果のキャッシュ
   * @param inc_flag  探索延長フラグ。true なら thpn/thdn を 1 回だけ無視して子局面を展開する
   * @return SearchResult  n の探索結果。この時点では tt_ にはに登録されていないので、
   *                            必要なら呼び出し側が登録する必要がある
   */
  template <bool kOrNode>
  SearchResult SearchImpl(Node& n, PnDn thpn, PnDn thdn, ChildrenCache& cache, bool inc_flag);

  /// 局面 n が詰む場合、最善応手列を返す。詰まない場合は {} を返す。
  std::vector<Move> CalcBestMoves(Node& n);

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

  void PrintProgress(const Node& n) const;

  bool IsSearchStop() const;

  TranspositionTable tt_;
  std::stack<MovePicker> pickers_{};
  std::stack<ChildrenCache> children_cache_{};

  std::atomic_bool* stop_{nullptr};
  Timer gc_timer_{};
  TimePoint last_gc_{};

  std::atomic_bool print_flag_{false};
  SearchProgress progress_{};
  std::uint64_t move_count_{};
  Score score_{};
  Depth max_depth_{kMaxNumMateMoves};
  int extra_search_count_{0};
  std::uint64_t max_search_node_{std::numeric_limits<std::uint64_t>::max()};

  /// 最善応手列（PV）の結果。CalcBestMoves() がそこそこ重いので、ここに保存しておく。
  std::vector<Move> best_moves_{};
  std::uint64_t tree_size_{};
};
}  // namespace komori

#endif  // KOMORING_HEIGHTS_HPP_
