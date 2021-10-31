#ifndef NODE_TRAVELS_HPP_
#define NODE_TRAVELS_HPP_

#include <unordered_set>
#include <unordered_map>

#include "typedefs.hpp"

namespace komori {

// forward declaration
class TTEntry;
class TranspositionTable;
class LookUpQuery;

/**
 * @brief remain_depth の深さを上限に n から静止探索を行う
 *
 * キャッシュを加味した高速 n 手詰めルーチンのような動きをする。これにより、局面 n の詰み／不詰が簡単に判断できる場合は
 * 探索を大幅にスキップすることができる。
 *
 * @param tt            置換表
 * @param n             現在の局面
 * @param depth         探索深さ。tt を引くために必要。
 * @param remain_depth  のこり探索深さ。0になるまで探索する。
 * @param query         局面 n の LookUp に使用する query。これを渡すことで証明駒／反証駒の登録が高速に行える
 * @return TTEntry*     探索結果。tt 内に存在しないエントリを返すことがあるので、次の tt の Lookup よりも前に
 *                      内容を確認する必要がある。
 */
template <bool kOrNode>
TTEntry* LeafSearch(TranspositionTable& tt, Position& n, Depth depth, Depth remain_depth, const LookUpQuery& query);

/**
 * @brief n の子孫ノードすべてに削除マーカーをつける
 *
 * @param tt       置換表
 * @param n        現在の局面
 * @param depth    現在の深さ。tt を引くために必要。
 * @param parents  root から訪れた局面の一覧。ループ検出に用いる
 * @param query    局面 n の LookUp に使用する query
 * @param entry    局面 n のエントリー
 */
template <bool kOrNode>
void MarkDeleteCandidates(TranspositionTable& tt,
                          Position& n,
                          Depth depth,
                          std::unordered_set<Key>& parents,
                          const LookUpQuery& query,
                          TTEntry* entry);

/**
 * @brief n の詰み手順を復元する
 *
 * @param tt      置換表
 * @param memo    最善手のメモ
 * @param n       現在の局面
 * @param depth   現在の深さ
 * @return int
 */
template <bool kOrNode>
int MateMovesSearch(TranspositionTable& tt, std::unordered_map<Key, Move>& memo, Position& n, int depth);

}  // namespace komori

#endif  // NODE_TRAVELS_HPP_