#ifndef KOMORI_MULTI_PV_HPP_
#define KOMORI_MULTI_PV_HPP_

#include <string>
#include <unordered_map>
#include <utility>

#include "../../usi.h"
#include "move_picker.hpp"
#include "node.hpp"
#include "typedefs.hpp"

namespace komori {

/**
 * @brief 複数の初手に対する最善応手列を保存する
 */
class MultiPv {
 public:
  /// Default constructor(default)
  MultiPv() = default;
  /// Copy constructor(delete)
  MultiPv(const MultiPv&) = delete;
  /// Move constructor(default)
  MultiPv(MultiPv&&) noexcept = default;
  /// Copy assign operator(delete)
  MultiPv& operator=(const MultiPv&) = delete;
  /// Move assign operator(default)
  MultiPv& operator=(MultiPv&&) noexcept = default;
  /// Destructor(default)
  ~MultiPv() = default;

  /**
   * @brief `node` の合法手からPVの初期化を行う
   * @param node 探索開始局面
   */
  void NewSearch(const Node& node) {
    pvs_.clear();
    for (const auto& move : MovePicker{node}) {
      auto depth_pv_pair = std::make_pair(0, USI::move(move));
      pvs_.insert(std::make_pair(move.move, std::move(depth_pv_pair)));
    }
  }

  /**
   * @brief 手 `move` に対し、最善応手列 `pv` とその時の探索深さ `depth` を更新する
   * @param move  更新する手
   * @param depth `move` に対する探索深さ
   * @param pv    `mov` に対する最善応手列（`move` を含む）
   * @pre `move` は `NewSearch(node)` で与えられた探索開始局面の合法手でなければならない
   */
  void Update(Move move, Depth depth, std::string_view pv) {
    if (auto it = pvs_.find(move); it != pvs_.end()) {
      it->second = std::make_pair(depth, std::string{pv});
    }
  }

  /**
   * @brief 手 `move` に対する、現在の最善応手列 `pv` とその時の探索深さ `depth` を取得する
   * @param move 結果を取得したい手
   * @return pair(探索深さ、PV)
   * @pre `move` は `NewSearch(node)` で与えられた探索開始局面の合法手でなければならない
   */
  const std::pair<Depth, std::string>& Get(Move move) const { return pvs_.at(move); }
  /**
   * @brief 手 `move` に対する、現在の最善応手列 `pv` とその時の探索深さ `depth` を取得する
   * @param move 結果を取得したい手
   * @return pair(探索深さ、PV)
   * @pre `move` は `NewSearch(node)` で与えられた探索開始局面の合法手でなければならない
   */
  const std::pair<Depth, std::string>& operator[](Move move) const { return Get(move); }

 private:
  /// 各合法手 `move` に対する探索深さと PV のペア
  std::unordered_map<Move, std::pair<Depth, std::string>> pvs_{};
};
}  // namespace komori

#endif  // KOMORI_MULTI_PV_HPP_
