/**
 * @file pv_list.hpp
 */
#ifndef KOMORI_PV_LIST_HPP_
#define KOMORI_PV_LIST_HPP_

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <vector>

#include "mate_len.hpp"
#include "move_picker.hpp"
#include "ranges.hpp"
#include "search_result.hpp"
#include "typedefs.hpp"

namespace komori {
/**
 * @brief 各手の探索結果とPVのリスト。
 *
 * 開始局面における合法手それぞれに対し、探索深さ、探索結果、PVをまとめたリストを管理する。リストは開始局面の
 * 手番側から見て良い順に並んでいる。すなわち、開始局面が OR node であれば「詰み -> 不明 -> 不詰」の順、
 * AND node であればだいたいその逆順で並ぶ。
 *
 * `LocalExpansion` でも探索結果を良い順で持っている。`LocalExpansion` と `PvList` との違いは、
 * `LocalExpansion` は余詰探索の探索情報が含まれている点である。余詰探索では局面が「N 手以下で詰むか？」を調べるが、
 * その最中では本来詰みである局面が不詰や不明として扱われることがある。一方、`PvList` では本探索、余詰探索を通して
 * 何手で詰むかを管理しており、実際にユーザーに見せる評価値としてはこちらの値を用いるべきである。
 *
 * 各手は pn=1, dn=1 の不明局面として初期化され、以下の4つのパターンにより評価値が更新される。
 *
 *   1. 不明  --> 不明（探索が進んで pn, dn が更新された）
 *   2. 不明  --> 詰み（探索が進んで詰みだと判明した）
 *   3. 不明  --> 不詰（探索が進んで不詰だと判明した）
 *   4. N手詰 --> M手詰（N>M、探索が進んで短い詰みが判明した）
 */
class PvList {
 public:
  /// 手の探索深さ、探索結果、PVをまとめた構造体
  struct PvInfo {
    Move move;             ///< 手
    Depth depth;           ///< 探索深さ
    SearchResult result;   ///< 探索結果
    std::vector<Move> pv;  ///< PV
  };

  /**
   * @brief 探索情報を削除する
   */
  void Clear() {
    pv_info_.clear();
    idx_.clear();
    move_to_raw_index_.clear();

    // pv_info_ と idx_ は高々 kDepthMax 要素なので shrink_to_fit() をする必要はない
  }

  /**
   * @brief 新しい探索を始める
   * @param n 探索開始局面
   *
   * n における合法手によりリストを初期化する。初期状態では、すべての手の PV は {move}、探索結果は pn=∞/2, dn=∞/2
   * である。
   */
  void NewSearch(const Node& n) {
    Clear();

    comparer_ = SearchResultComparer(n.IsOrNode());
    is_sorted_ = true;  // 初期状態はすべて同じ評価値なのでソートされた状態である。

    MovePicker mp{n};
    pv_info_.reserve(mp.size());
    idx_.reserve(mp.size());
    for (const auto& [i_raw, move] : WithIndex(mp)) {
      const SearchResult result =
          SearchResult::MakeFirstVisit(kInfinitePnDn / 2, kInfinitePnDn / 2, kDepthMaxMateLen, 1);
      PvInfo info{move, 1, result, {move}};
      pv_info_.emplace_back(std::move(info));

      move_to_raw_index_.emplace(move, i_raw);
      idx_.emplace_back(i_raw);
    }
  }

  /**
   * @brief 手 `move` に対する探索結果を更新する
   * @param move   手
   * @param result 探索結果
   * @param depth  探索深さ（optional）
   * @param pv     PV（optional）
   * @pre `move` は `n` における合法手
   *
   * 探索深さ `depth` は、`result` の内容に関係なく（値が nullopt でなければ）必ず代入する。一方、`pv` と
   * `result` については、`info.result.IsFinal()` から `!result.IsFinal()` へ遷移しようとした場合は更新しない。
   */
  void Update(Move move,
              const SearchResult& result,
              std::optional<Depth> depth = std::nullopt,
              std::optional<std::vector<Move>> pv = std::nullopt) {
    const auto i_raw = move_to_raw_index_.at(move);
    auto& info = pv_info_[i_raw];
    if (depth) {
      info.depth = depth.value();
    }

    // `result` が final から not final へ遷移しようとしているときは内容を更新しない
    if (result.IsFinal() || !info.result.IsFinal()) {
      is_sorted_ = false;
      info.result = result;
      if (pv) {
        info.pv = std::move(pv.value());
      }
    }
  }

  /**
   * @brief 手 `move` が証明済み（詰み）かどうかを返す
   * @param move 手
   * @return `move` が証明済み（詰み）かどうか
   * @pre `move` は `n` における合法手
   */
  bool IsProven(Move move) const {
    const auto i_raw = move_to_raw_index_.at(move);
    const auto& info = pv_info_[i_raw];
    return info.result.Pn() == 0;
  }

  /**
   * @brief 各手の探索結果とPVをまとめた配列を返す。
   * @return 探索結果とPVをまとめた配列。（手番側から見て評価値がよい順に並んでいる）
   */
  std::vector<PvInfo> GetPvList() {
    SortIfNeeded();
    std::vector<PvInfo> ret;
    ret.reserve(pv_info_.size());

    for (const auto i_raw : idx_) {
      ret.push_back(pv_info_[i_raw]);
    }

    return ret;
  }

  /**
   * @brief 開始局面における PV を返す
   * @return 開始局面における PV
   * @note 開始局面に合法手がないとき、空配列を返す。
   */
  std::vector<Move> BestMoves() {
    if (idx_.empty()) {
      return {};
    }

    SortIfNeeded();  // 必要なのは最善手だけだが、いずれ全体の並び替えが必要になる可能性が高いのでここでやっておく。
    return pv_info_[idx_[0]].pv;
  }

 private:
  /**
   * @brief `idx_` を `comparer_` に従って評価値がいい順に並べ替える
   */
  void SortIfNeeded() {
    if (!is_sorted_) {
      std::stable_sort(idx_.begin(), idx_.end(), [this](const auto& lhs, const auto& rhs) {
        const auto& lhs_info = pv_info_[lhs];
        const auto& rhs_info = pv_info_[rhs];
        const auto ordering = comparer_(lhs_info.result, rhs_info.result);
        return ordering == SearchResultComparer::Ordering::kLess;
      });
      is_sorted_ = true;
    }
  }

  SearchResultComparer comparer_{true};                        ///< 探索結果の比較器
  std::unordered_map<Move, std::uint32_t> move_to_raw_index_;  ///< 手 move から i_raw への逆引きテーブル
  std::vector<std::uint32_t> idx_;  ///< i_raw を評価値順（`comparer_` 順）に並べたもの
  std::vector<PvInfo> pv_info_;  ///< 各手の探索結果と PV。MovePicker による手の生成結果と同じ順番に並んでいる
  bool is_sorted_{true};  ///< `idx_` が評価値順に並んでいるかどうか
};
}  // namespace komori

#endif  // KOMORI_PV_LIST_HPP_
