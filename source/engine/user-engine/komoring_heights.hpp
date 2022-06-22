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
#include "circular_array.hpp"
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
  void NewSearch(std::uint64_t gc_interval);

  void Visit(Depth depth) { depth_ = std::max(depth_, depth); }
  void Tick();
  UsiInfo GetInfo() const;
  std::uint64_t MoveCount() const { return thread_->nodes; }
  bool ShouldStop() const { return MoveCount() >= move_limit_ || stop_; }

  bool ShouldGc() const { return MoveCount() >= next_gc_count_; }
  void ResetNextGc();

  /// 探索局面数上限を move_limit 以下にする。PushLimit() は探索中に再帰的に複数回呼ぶことができる。
  void PushLimit(std::uint64_t move_limit);
  /// Pop されていない最も直近の PushLimit コールを巻き戻し、探索上限を復元する
  void PopLimit();

  void SetStop(bool stop = true) { stop_ = stop; }

 private:
  static constexpr inline std::size_t kHistLen = 16;

  std::atomic_bool stop_{false};

  std::chrono::system_clock::time_point start_time_;
  Depth depth_;

  CircularArray<std::chrono::system_clock::time_point, kHistLen> tp_hist_;
  CircularArray<std::uint64_t, kHistLen> mc_hist_;
  std::size_t hist_idx_;

  std::uint64_t move_limit_;
  std::stack<std::uint64_t> limit_stack_;
  std::uint64_t gc_interval_;
  std::uint64_t next_gc_count_;
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
  /// 見つけた詰み手順を返す
  const auto& BestMoves() const { return best_moves_; }
  /// 現在の探索情報（npsやhashfullなど）を返す。hashfull の計算があるので過度に頻繁に呼び出さないこと
  UsiInfo CurrentInfo() const;

  /// df-pn 探索本体。局面 n が詰むかを調べる
  NodeState Search(Position& n, bool is_root_or_node);

  // <Debug用>
  void ShowValues(Position& n, bool is_root_or_node, const std::vector<Move>& moves);
  void ShowPv(Position& n, bool is_root_or_node);
  // </Debug用>

 private:
  /// 余詰め探索。n が alpha 手以上 beta 手以下で詰むかどうかを調べる
  std::pair<MateLen, Depth> PostSearch(std::unordered_map<Key, int>& visit_count, Node& n, MateLen alpha, MateLen beta);

  // <探索エントリポイント>
  // SearchImpl （再帰関数）の前処理・後処理を担う関数たち。
  // 探索局面数上限や局面を微調整したいので、エントリポイントが複数存在する。

  /// 通常探索のエントリポイント。n が詰むかどうかを調べる
  SearchResult SearchEntry(Node& n, PnDn thpn = kInfinitePnDn, PnDn thdn = kInfinitePnDn);
  /// 余詰め探索のエントリポイント。n.DoMove(move) した局面が詰むかどうかを調べる
  SearchResult PostSearchEntry(Node& n, Move move);
  /// 無駄合い探索のエントリポイント。n.DoMove(move) し、取った駒を相手にプレゼントした局面が詰むかどうかを調べる
  SearchResult UselessDropSearchEntry(Node& n, Move move);
  // </探索エントリポイント>

  /// 探索本体
  SearchResult SearchImpl(Node& n, PnDn thpn, PnDn thdn, ChildrenCache& cache, bool inc_flag);

  std::vector<Move> TraceBestMove(Node& n);

  void PrintYozume(Node& n, const std::vector<Move>& pv);
  /// print_flag_ が立っていたら出力してフラグを消す。立っていなかったら何もしない。
  void PrintIfNeeded(const Node& n);

  TranspositionTable tt_{};
  EngineOption option_;

  detail::SearchMonitor monitor_{};
  Score score_{};
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
