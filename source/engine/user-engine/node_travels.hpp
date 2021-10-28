#ifndef NODE_TRAVELS_HPP_
#define NODE_TRAVELS_HPP_

#include <unordered_set>

#include "typedefs.hpp"

namespace komori {

// forward declaration
class TTEntry;
class TranspositionTable;
class LookUpQuery;

template <bool kOrNode>
TTEntry* LeafSearch(TranspositionTable& tt, Position& n, Depth depth, Depth remain_depth, const LookUpQuery& query);

template <bool kOrNode>
void MarkDeleteCandidates(TranspositionTable& tt,
                          Position& n,
                          Depth depth,
                          std::unordered_set<Key>& parents,
                          const LookUpQuery& query,
                          TTEntry* entry);

template <bool kOrNode>
int MateMovesSearch(TranspositionTable& tt, std::unordered_map<Key, Move>& memo, Position& n, int depth);

}  // namespace komori

#endif  // NODE_TRAVELS_HPP_