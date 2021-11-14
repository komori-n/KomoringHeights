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
  static inline constexpr Depth kNotSearching = Depth{kMaxNumMateMoves + 1};
  static inline constexpr Depth kRepResult = Depth{kMaxNumMateMoves + 2};
  static inline constexpr Depth kNoMateLen = Depth{-1};
  struct NodeCache {
    Move move{MOVE_NONE};
    Depth depth{kNotSearching};
    Depth mate_len{kNoMateLen};
    int surplus_count{0};
  };

  void DoMove(Position& n, Move move, Depth depth) { n.do_move(move, st_info_[depth]); }
  void UndoMove(Position& n, Move move) { n.undo_move(move); }

  /**
   * @brief Pv（最善応手列）を再帰的に探索する
   *
   * @param memo      探索結果のメモ
   * @param n         現局面
   * @param depth     探索深さ
   * @param path_key  局面ハッシュ
   * @return std::pair<NodeCache, Depth>
   *     first   局面の探索結果
   *     second  firstが千日手絡みの評価値の場合、千日手がスタートした局面の深さ。
   *             firstが千日手絡みの評価値ではない場合、kNotSearching。
   *
   * @note
   * 千日手をうまく回避するために、戻り値や NodeCache の型を工夫する必要がある。
   * 例えば、以下の局面 O3 を考える。
   *
   *                     ...
   *                     |
   *                     v
   *     O1 <-- A2  <--  O3  -->  A6
   *     |               ^        |
   *     v               |        v
   *    mate             A4  <--  O5 --> A14
   *
   * ===
   * O: OR node, A: And node. 局面の右の数字は詰み手数
   *
   * O3-->A2-->O1 が求めたいPV。ここで、O3->A6->O5->A4 を先に探索すると、O3 に戻ってきてしまう。この探索で
   * 特に O5 に注目すると、O5 は A14 から 15 手詰みに見えてしまう。（正しくは O5-->A4-->O3-->A2-->O1 の 5 手詰め）
   * もしこの結果を memo に保存して再利用すると、O5 が本来よりも詰みづらいノードだと認識されてしまうことになる。
   * よって、このようなケースを区別できるようにしなければならない。
   */
  template <bool kOrNode>
  std::pair<NodeCache, Depth> MateMovesSearchImpl(std::unordered_map<Key, NodeCache>& memo,
                                                  Position& n,
                                                  Depth depth,
                                                  Key path_key);

  TranspositionTable& tt_;
  std::stack<MovePicker> pickers_{};
  std::array<StateInfo, kMaxNumMateMoves> st_info_{};
};
}  // namespace komori

#endif  // NODE_TRAVELS_HPP_