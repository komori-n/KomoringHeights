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
/**
 * @brief 探索の進捗状況をチェックするクラス
 *
 * nps の計測や探索局面数上限のチェックを行う。
 *
 * 探索局面数上限の設定は PushLimit/PopLimit で行う。名前が示すとおり、stack のように探索局面数の上書きや復元を
 * 行うことができる。これは、余詰探索のような一時的に探索局面数を制限する使用方法を想定している機能である。
 */
class SearchMonitor {
 public:
  void Init(Thread* thread);
  void NewSearch();

  void Visit(Depth depth) { depth_ = std::max(depth_, depth); }
  UsiInfo GetInfo() const;
  std::uint64_t MoveCount() const { return thread_->nodes; }
  bool ShouldStop() const { return MoveCount() >= move_limit_ || stop_; }

  /// 探索局面数上限を move_limit 以下にする。PushLimit() は探索中に再帰的に複数回呼ぶことができる。
  void PushLimit(std::uint64_t move_limit);
  /// Pop されていない最も直近の PushLimit コールを巻き戻し、探索上限を復元する
  void PopLimit();

  void SetStop(bool stop = true) { stop_ = stop; }

 private:
  std::atomic_bool stop_{false};

  std::chrono::system_clock::time_point start_time_;
  Depth depth_;
  std::uint64_t move_limit_;
  std::stack<std::uint64_t> limit_stack_;
  Thread* thread_;
};
}  // namespace detail

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
  void Init(EngineOption option, Thread* thread);
  /// 詰将棋探索をやめさせる
  void SetStop() { monitor_.SetStop(true); }
  /// stopフラグをクリアする
  void ResetStop() { monitor_.SetStop(false); }

  /// 探索情報のPrintを指示する。Printが完了したらフラグはfalseになる
  void RequestPrint() { print_flag_ = true; }

  /// df-pn 探索本体。局面 n が詰むかを調べる
  NodeState Search(Position& n, bool is_root_or_node);
  /// 見つけた詰み手順を返す
  const auto& BestMoves() const { return best_moves_; }

  void ShowValues(Position& n, bool is_root_or_node, const std::vector<Move>& moves);
  void ShowPv(Position& n, bool is_root_or_node);

  UsiInfo CurrentInfo() const;

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

  /// print_flag_ が立っていたら出力してフラグを消す。立っていなかったら何もしない。
  void PrintIfNeeded(const Node& n);

  TranspositionTable tt_{};
  EngineOption option_;

  detail::SearchMonitor monitor_{};
  Score score_{};
  std::uint64_t next_gc_count_{0};
  std::atomic_bool print_flag_{false};

  /// 最善応手列（PV）の結果。CalcBestMoves() がそこそこ重いので、ここに保存しておく。
  std::vector<Move> best_moves_{};
  PvTree pv_tree_{};

  // <一時変数>
  // 探索中に使用する一時変数。本当はスタック上に置きたいが、スタックオーバーフローしてしまうのでメンバで持つ。
  std::stack<ChildrenCache> children_cache_{};
  std::stack<MovePicker> pickers_{};
  // </一時変数>
};
}  // namespace komori

#endif  // KOMORING_HEIGHTS_HPP_
