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
#include "move_selector.hpp"
#include "node_travels.hpp"
#include "transposition_table.hpp"
#include "ttcluster.hpp"

namespace komori {
// forward decleration
class NodeHistory;

/// Selector を格納する領域。stack に積むと stackoverflow になりがちなため
class MoveSelectorCache {
 public:
  void Resize(std::size_t size) {
    or_selectors_.reserve(size);
    and_selectors_.reserve(size);
  }

  template <bool kOrNode, typename... Args>
  auto& EmplaceBack(Args&&... args) {
    if constexpr (kOrNode) {
      return or_selectors_.emplace_back(std::forward<Args>(args)...);
    } else {
      return and_selectors_.emplace_back(std::forward<Args>(args)...);
    }
  }

  template <bool kOrNode>
  void PopBack() {
    if constexpr (kOrNode) {
      or_selectors_.pop_back();
    } else {
      and_selectors_.pop_back();
    }
  }

 private:
  std::vector<MoveSelector<true>> or_selectors_{};
  std::vector<MoveSelector<false>> and_selectors_{};
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

  /// 探索情報のPrintを指示する。Printが完了したらフラグはfalseになる
  void SetPrintFlag() { print_flag_ = true; }

  /// df-pn 探索本体。局面 n が詰むかを調べる
  bool Search(Position& n, std::atomic_bool& stop_flag);

  /// 局面 n が詰む場合、最善応手列を返す。詰まない場合は {} を返す。
  std::vector<Move> BestMoves(Position& n);

  std::string Info(int depth) const;
  void PrintDebugInfo() const;

 private:
  /**
   * @brief df-pn 探索の本体。pn と dn がいい感じに小さいノードから順に最良優先探索を行う。
   *
   * @tparam kOrNode OrNode（詰ます側）なら true、AndNode（詰まされる側）なら false
   * @param n 現局面
   * @param thpn pn のしきい値。n の探索中に pn がこの値以上になったら探索を打ち切る。
   * @param thpn dn のしきい値。n の探索中に dn がこの値以上になったら探索を打ち切る。
   * @param depth 探索深さ
   * @param node_history root から現局面までで通過した局面の一覧。千日手・劣等局面の判定に用いる。
   * @param query 現局面の置換表クエリ。引数として渡すことで高速化をはかる。
   * @param entry 現局面の CommonEntry。引数として渡すことで LookUp 回数をへらすことができる。
   * @param inc_flag infinite loopが懸念されるときはtrue。探索を延長する。
   */
  template <bool kOrNode>
  void SearchImpl(Position& n,
                  PnDn thpn,
                  PnDn thdn,
                  Depth depth,
                  NodeHistory& node_history,
                  const LookUpQuery& query,
                  CommonEntry* entry,
                  bool inc_flag);

  void PrintProgress(const Position& n, Depth depth) const;

  TranspositionTable tt_{};
  NodeTravels node_travels_{tt_};
  MoveSelectorCache selector_cache_{};
  std::array<StateInfo, kMaxNumMateMoves> st_info_{};

  std::atomic_bool* stop_{nullptr};
  std::atomic_bool print_flag_{false};
  std::uint64_t searched_node_{};
  Depth searched_depth_{};
  std::chrono::system_clock::time_point start_time_;
  Depth max_depth_{kMaxNumMateMoves};
  std::uint64_t max_search_node_{std::numeric_limits<std::uint64_t>::max()};
};
}  // namespace komori

#endif  // KOMORING_HEIGHTS_HPP_
