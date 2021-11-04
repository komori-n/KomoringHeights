#ifndef NODE_TRAVELS_HPP_
#define NODE_TRAVELS_HPP_

#include <array>
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
  NodeTravels(TranspositionTable& tt);
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
   * @param memo    最善手のメモ
   * @param n       現在の局面
   * @param depth   現在の深さ
   * @param path_key      経路依存のハッシュ
   * @return std::pair<int, int> 詰み手数（詰まない場合マイナス）とそのときの駒あまりの枚数
   */
  template <bool kOrNode>
  std::pair<int, int> MateMovesSearch(std::unordered_map<Key, Move>& memo, Position& n, int depth, Key path_key);

 private:
  template <bool kOrNode>
  MovePicker<kOrNode>& PushMovePicker(Position& n);
  template <bool kOrNode>
  void PopMovePicker();

  void DoMove(Position& n, Move move, Depth depth) { n.do_move(move, st_info_[depth]); }
  void UndoMove(Position& n, Move move) { n.undo_move(move); }

  TranspositionTable& tt_;
  std::vector<MovePicker<true>> or_pickers_{};
  std::vector<MovePicker<false>> and_pickers_{};
  std::array<StateInfo, kMaxNumMateMoves> st_info_{};
};
}  // namespace komori

#endif  // NODE_TRAVELS_HPP_