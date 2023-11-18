/**
 * @file komoring_heights.hpp
 */
#ifndef KOMORI_KOMORING_HEIGHTS_HPP_
#define KOMORI_KOMORING_HEIGHTS_HPP_

#include <vector>

#include "engine_option.hpp"
#include "expansion_stack.hpp"
#include "pv_list.hpp"
#include "score.hpp"
#include "search_monitor.hpp"
#include "search_result.hpp"
#include "transposition_table.hpp"
#include "usi_info.hpp"

namespace komori {
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
   * @brief 現時点の探索結果から詰め手順を取得する
   * @param n 現局面
   * @param len 詰み手数の上限値
   * @param exact 正確に len 手詰を取得するか（default: false）
   * @return 詰み手順
   * @pre メインスレッドから呼び出すこと
   */
  std::vector<Move> GetMatePath(Node& n, MateLen len, bool exact = false);

  /**
   * @brief OR node `n` における len 手以下詰めの最善手とそのときの詰み手数を取得する
   * @param n 現局面
   * @param len 詰み手数の上限値
   * @param exact 正確に len 手詰を取得するか
   * @return 最善手とそのときの詰み手数
   */
  std::pair<Move, MateLen> GetBestMoveOrNode(Node& n, MateLen len, bool exact);
  /**
   * @brief AND node `n` における len 手以下詰めの最善手とそのときの詰み手数を取得する
   * @param n 現局面
   * @param len 詰み手数の上限値
   * @param exact 正確に len 手詰を取得するか
   * @return 最善手とそのときの詰み手数
   */
  std::pair<Move, MateLen> GetBestMoveAndNode(Node& n, MateLen len, bool exact);

  /**
   * @brief `move` に対する PV を構成して `pv_list_` を更新する
   * @param n       現局面
   * @param move    PV に追加する手
   * @param result  探索結果
   *
   * `move` が詰みのとき、詰みに至るまでの手順を1つ `pv_list_` へ登録する。このとき、`result.Len()` に書かれた
   * 手数以下の詰み手順が登録されることは保証されているが、厳密に `result.Len()` 手詰みであることは保証されない。
   *
   * `move` が（余詰探索関係なく）不詰のとき、`n` が OR node であれば `move` 直後に詰みを逃れる応手を PV へ追加して
   * 結果を登録する。
   */
  void UpdateFinalPv(Node& n, Move move, const SearchResult& result);

  /// 現在の探索情報を取得する
  /// @pre メインスレッドから呼び出すこと
  UsiInfo CurrentInfo() const;

  /**
   * @brief 探索情報を出力する
   * @param n 現局面
   * @pre メインスレッドから呼び出すこと
   */
  void Print(const Node& n);

  tt::TranspositionTable tt_;  ///< 置換表
  EngineOption option_;        ///< エンジンオプション
  bool pv_search_{false};      ///< 現在PV探索中かどうか

  SearchMonitor monitor_;  ///< 探索モニター

  std::vector<Move> best_moves_;                 ///< 詰み手順
  std::deque<ExpansionStack> expansion_list_{};  ///< 局面展開のための一時領域
  Score score_{};  ///< 現在の探索評価値。余詰探索中に CurrentInfo() で取得できるようにここにおいておく

  PvList pv_list_;  ///< 各手に対する PV の一覧
};
}  // namespace komori

#endif  // KOMORI_KOMORING_HEIGHTS_HPP_
