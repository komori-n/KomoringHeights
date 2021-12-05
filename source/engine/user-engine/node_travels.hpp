#ifndef NODE_TRAVELS_HPP_
#define NODE_TRAVELS_HPP_

#include <array>
#include <stack>
#include <unordered_map>
#include <unordered_set>

#include "move_picker.hpp"
#include "typedefs.hpp"

namespace komori {

// forward declaration
class CommonEntry;
class TranspositionTable;
class LookUpQuery;
class Node;

class NodeTravels {
 public:
  explicit NodeTravels(TranspositionTable& tt);
  NodeTravels() = delete;
  NodeTravels(const NodeTravels&) = delete;
  NodeTravels(NodeTravels&&) = delete;
  NodeTravels& operator=(const NodeTravels&) = delete;
  NodeTravels& operator=(NodeTravels&&) = delete;
  ~NodeTravels() = default;

  /**
   * @brief n の詰み手順を復元する
   *
   * @param n       現在の局面
   * @return std::vector<Move> 詰み手順
   */
  std::vector<Move> MateMovesSearch(Node& n);

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

  void DoMove(Position& n, Move move, Depth depth) { n.do_move(move, st_info_[depth]); }
  void UndoMove(Position& n, Move move) { n.undo_move(move); }

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

  TranspositionTable& tt_;
  std::stack<MovePicker> pickers_{};
  std::array<StateInfo, kMaxNumMateMoves> st_info_{};
};
}  // namespace komori

#endif  // NODE_TRAVELS_HPP_