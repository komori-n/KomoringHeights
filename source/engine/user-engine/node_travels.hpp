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
   * @brief remain_depth の深さを上限に n から静止探索を行う
   *
   * キャッシュを加味した高速 n 手詰めルーチンのような動きをする。これにより、局面 n
   * の詰み／不詰が簡単に判断できる場合は 探索を大幅にスキップすることができる。
   *
   * @param num_searches  現在の探索局面数
   * @param n             現在の局面
   * @param depth         探索深さ。tt を引くために必要。
   * @param remain_depth  のこり探索深さ。0になるまで探索する。
   * @param query         局面 n の LookUp に使用する query。これを渡すことで証明駒／反証駒の登録が高速に行える
   * @return CommonEntry*     探索結果。tt 内に存在しないエントリを返すことがあるので、次の tt の Lookup よりも前に
   *                      内容を確認する必要がある。
   */
  template <bool kOrNode>
  CommonEntry* LeafSearch(std::uint64_t num_searches,
                          Position& n,
                          Depth depth,
                          Depth remain_depth,
                          const LookUpQuery& query);

  /**
   * @brief n の詰み手順を復元する
   *
   * @param n       現在の局面
   * @param depth   現在の深さ
   * @param path_key      経路依存のハッシュ
   * @return std::vector<Move> 詰み手順
   */
  std::vector<Move> MateMovesSearch(Position& n, Depth depth, Key path_key);

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
   * @param depth           探索深さ
   * @param path_key        局面ハッシュ
   * @return std::pair<NodeCache, Depth>
   *     first   局面の探索結果
   *     second  firstが千日手絡みの評価値の場合、千日手がスタートした局面の深さ。
   *             firstが千日手絡みの評価値ではない場合、kNonRepetitionDepth。
   */
  template <bool kOrNode>
  std::pair<NumMoves, Depth> MateMovesSearchImpl(std::unordered_map<Key, MateMoveCache>& mate_table,
                                                 std::unordered_map<Key, Depth>& search_history,
                                                 Position& n,
                                                 Depth depth,
                                                 Key path_key);

  TranspositionTable& tt_;
  std::stack<MovePicker> pickers_{};
  std::array<StateInfo, kMaxNumMateMoves> st_info_{};
};
}  // namespace komori

#endif  // NODE_TRAVELS_HPP_