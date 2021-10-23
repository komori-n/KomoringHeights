#ifndef LEAF_SEARCH_HPP_
#define LEAF_SEARCH_HPP_

#include "typedefs.hpp"

namespace komori {

// forward declaration
class TTEntry;
class TranspositionTable;
class LookUpQuery;

template <bool kOrNode>
TTEntry* LeafSearch(TranspositionTable& tt, Position& n, Depth depth, Depth remain_depth, const LookUpQuery& query);

}  // namespace komori

#endif  // LEAF_SEARCH_HPP_