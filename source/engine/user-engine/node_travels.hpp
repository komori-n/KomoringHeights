#ifndef NODE_TRAVELS_HPP_
#define NODE_TRAVELS_HPP_

#include <array>
#include <unordered_map>
#include <unordered_set>

#include "move_picker.hpp"
#include "typedefs.hpp"

namespace komori {

// forward declaration
class TTEntry;
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
   * @param n             現在の局面
   * @param depth         探索深さ。tt を引くために必要。
   * @param remain_depth  のこり探索深さ。0になるまで探索する。
   * @param query         局面 n の LookUp に使用する query。これを渡すことで証明駒／反証駒の登録が高速に行える
   * @return TTEntry*     探索結果。tt 内に存在しないエントリを返すことがあるので、次の tt の Lookup よりも前に
   *                      内容を確認する必要がある。
   */
  template <bool kOrNode>
  TTEntry* LeafSearch(Position& n, Depth depth, Depth remain_depth, const LookUpQuery& query);

  /**
   * @brief n の子孫ノードすべてに削除マーカーをつける
   *
   * @param n        現在の局面
   * @param depth    現在の深さ。tt を引くために必要。
   * @param parents  root から訪れた局面の一覧。ループ検出に用いる
   * @param query    局面 n の LookUp に使用する query
   * @param entry    局面 n のエントリー
   */
  template <bool kOrNode>
  void MarkDeleteCandidates(Position& n,
                            Depth depth,
                            std::unordered_set<Key>& parents,
                            const LookUpQuery& query,
                            TTEntry* entry);

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