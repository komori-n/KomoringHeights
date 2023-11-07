﻿/**
 * @file komoring_heights.hpp
 */
#ifndef KOMORI_KOMORING_HEIGHTS_HPP_
#define KOMORI_KOMORING_HEIGHTS_HPP_

#include <algorithm>
#include <chrono>
#include <stack>
#include <vector>

#include "../../thread.h"
#include "circular_array.hpp"
#include "engine_option.hpp"
#include "expansion_stack.hpp"
#include "multi_pv.hpp"
#include "periodic_alarm.hpp"
#include "score.hpp"
#include "search_result.hpp"
#include "transposition_table.hpp"
#include "usi_info.hpp"

namespace komori {
namespace detail {
/**
 * @brief 探索局面数を観測して nps を計算したり探索中断の判断をしたりするクラス。
 */
class SearchMonitor {
 public:
  /**
   * @brief 変数を初期化して探索を開始する。
   * @param hashfull_check_interval 置換表使用率チェック周期（/探索局面数）
   */
  void NewSearch(std::uint64_t hashfull_check_interval, std::uint64_t pv_interval, std::uint64_t move_limit);

  /**
   * @brief 深さ `depth` の局面に訪れたことを報告する
   * @param depth 深さ
   */
  void Visit(Depth depth) {
    max_depth_.store(std::max(max_depth_.load(std::memory_order_relaxed), depth), std::memory_order_relaxed);
  }

  /**
   * @brief 現在の探索情報を `UsiInfo` に詰めて返す。
   * @return 現在の探索情報
   */
  UsiInfo GetInfo() const;

  /// 現在の探索局面数
  std::uint64_t MoveCount() const { return Threads.nodes_searched(); }
  /// 今すぐ置換表使用率をチェックすべきなら true
  bool ShouldCheckHashfull();
  /// 次回の置換表使用率チェックタイミングを更新する
  void ResetNextHashfullCheck();
  /// 今すぐ探索をやめるべきなら true
  bool ShouldStop();
  /// 今すぐ評価値を出力すべきかどうか。定期的に呼び出す必要がある。
  bool ShouldPrint();

 private:
  /// nps の計算のために保持する探索局面数の履歴数
  static constexpr inline std::size_t kHistLen = 16;

  std::chrono::system_clock::time_point start_time_;  ///< 探索開始時刻

  CircularArray<std::chrono::system_clock::time_point, kHistLen> tp_hist_;  ///< mc_hist_ を観測した時刻
  CircularArray<std::uint64_t, kHistLen> mc_hist_;                          ///< 各時点での探索局面数
  std::size_t hist_idx_;  ///< `tp_hist_` と `mc_hist_` の現在の添字

  std::uint64_t move_limit_;               ///< 探索局面数の上限
  TimePoint time_limit_;                   ///< 探索の時間制限[ms]
  std::uint64_t hashfull_check_interval_;  ///< 置換表使用率をチェックする周期[探索局面数]
  std::uint32_t hashfull_check_skip_;      ///< next_hashfull_check_ のチェックをスキップする回数
  std::uint64_t next_hashfull_check_;  ///< 次に置換表使用率をチェックするタイミング[探索局面数]

  PeriodicAlarm print_alarm_;  ///< PV出力用のタイマー
  PeriodicAlarm stop_check_;   ///< 探索停止判断用のタイマー

  alignas(64) std::atomic<bool> stop_;  ///< 探索中止状態かどうか
  std::atomic<Depth> max_depth_;        ///< 最大探索深さ
};
}  // namespace detail

/**
 * @brief 詰将棋探索の本体
 */
class KomoringHeights {
 public:
  /// Default constructor(default)
  KomoringHeights() = default;
  /// Copy constructor(delete)
  KomoringHeights(const KomoringHeights&) = delete;
  /// Move constructor(delete)
  KomoringHeights(KomoringHeights&&) = delete;
  /// Copy assign operator(delete)
  KomoringHeights& operator=(const KomoringHeights&) = delete;
  /// Move assign operator(delete)
  KomoringHeights& operator=(KomoringHeights&&) = delete;
  /// Destructor(default)
  ~KomoringHeights() = default;

  /**
   * @brief エンジンを初期化する
   * @param option 探索オプション
   * @param num_threads スレッド数
   */
  void Init(const EngineOption& option, std::uint32_t num_threads);
  /// 置換表の内容をすべて削除する。ベンチマーク用。
  void Clear();

  /**
   * @brief 詰み手順を取得する
   * @pre Search() の戻り値が `NodeState::kProven`
   * @return 詰み手順
   */
  const std::vector<Move>& BestMoves() const { return best_moves_; }

  /**
   * @brief Search() の準備を行う。探索開始直前に main_thread から呼び出すこと。
   * @param n 現局面
   * @param is_root_or_node `n` が OR node かどうか
   * @pre メインスレッドから呼び出すこと
   */
  void NewSearch(const Position& n, bool is_root_or_node);

  /**
   * @brief 詰め探索を行う。（探索本体）
   * @param n 現局面
   * @param is_root_or_node `n` が OR node かどうか
   * @return 探索結果
   */
  NodeState Search(const Position& n, bool is_root_or_node);

 private:
  /**
   * @brief 詰み手順を探す
   * @param n 現局面
   * @return 探索結果と詰み手数
   *
   * `SearchEntry()` や `SearchImpl()` のような df-pn 探索では「詰みかどうか」の探索は得意だが
   * 「最短の詰み手順かどうか」の判定は難しい。この関数では、詰み手数を変えながら `SearchEntry()` を
   * 呼ぶことで局面 `n` の詰み手数の区間を狭めていくことが目的の関数である。
   */
  std::pair<NodeState, MateLen> SearchMainLoop(Node& n);

  /**
   * @brief `n` が `len` 手以下で詰むかを探索する
   * @param n 現局面
   * @param len 詰み手数
   * @return 探索結果
   *
   * `SearchImpl()` による再帰探索のエントリポイント。しきい値をいい感じに変化させることで探索の途中経過を
   * 標準出力に出しながら探索を進めることができる。
   */
  SearchResult SearchEntry(Node& n, MateLen len);

  /**
   * @brief 詰め探索の本体。root node専用の `SearchImpl()`。
   * @param n 現局面（root node）
   * @param thpn pn のしきい値
   * @param thdn dn のしきい値
   * @param len  残り手数
   * @return 探索結果
   */
  SearchResult SearchImplForRoot(Node& n, PnDn thpn, PnDn thdn, MateLen len);

  /**
   * @brief 詰め探索の本体。（再帰関数）
   * @param n 現局面
   * @param thpn pn のしきい値
   * @param thdn dn のしきい値
   * @param len  残り手数
   * @param inc_flag TCA の探索延長フラグ
   * @return 探索結果
   */
  SearchResult SearchImpl(Node& n, PnDn thpn, PnDn thdn, MateLen len, std::uint32_t& inc_flag);

  /**
   * @brief `n` が AND node かつ不詰のとき、不詰になるような手を1つ返す
   * @param n 現局面
   * @return 不詰になる応手
   * @pre メインスレッドから呼び出すこと
   */
  std::optional<Move> GetEvasion(Node& n);

  /**
   * @brief 現時点の探索結果から詰め手順を取得する
   * @param n 現局面
   * @param len 詰み手数の上限値
   * @return 詰み手順
   * @pre メインスレッドから呼び出すこと
   */
  std::vector<Move> GetMatePath(Node& n, MateLen len);

  /// 現在の探索情報を取得する
  /// @pre メインスレッドから呼び出すこと
  UsiInfo CurrentInfo() const;

  /**
   * @brief 探索情報を出力する
   * @param n 現局面
   * @param force_print show_pv_after_mate オプションがついていても構わず出力するか[default=false]
   * @pre メインスレッドから呼び出すこと
   */
  void Print(const Node& n, bool force_print = false);

  /**
   * @brief Root 局面 `n` における探索情報を出力する
   * @param n 探索開始局面
   * @pre メインスレッドから呼び出すこと
   */
  void PrintAtRoot(const Node& n);

  tt::TranspositionTable tt_;  ///< 置換表
  EngineOption option_;        ///< エンジンオプション

  detail::SearchMonitor monitor_;  ///< 探索モニター

  std::vector<Move> best_moves_;                 ///< 詰み手順
  std::deque<ExpansionStack> expansion_list_{};  ///< 局面展開のための一時領域
  bool after_final_{false};                      ///< 余詰探索中かどうか
  Score score_{};  ///< 現在の探索評価値。余詰探索中に CurrentInfo() で取得できるようにここにおいておく

  MultiPv multi_pv_;  ///< 各手に対する PV の一覧
};
}  // namespace komori

#endif  // KOMORI_KOMORING_HEIGHTS_HPP_
