#ifndef PV_TREE_HPP_
#define PV_TREE_HPP_

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "node.hpp"
#include "typedefs.hpp"

namespace komori {
class PvTree {
 public:
  struct Entry {
    Bound bound;
    MateLen mate_len;
    Move best_move;
  };

  PvTree() = default;
  PvTree(const PvTree&) = delete;
  PvTree(PvTree&&) = delete;
  PvTree& operator=(const PvTree&) = delete;
  PvTree& operator=(PvTree&&) = delete;
  ~PvTree() = default;

  void Clear();
  void Insert(Node& n, const Entry& entry);
  std::optional<Entry> Probe(Node& n) const;
  std::optional<Entry> ProbeAfter(Node& n, Move move) const;
  std::vector<Move> Pv(Node& n) const;

  void PrintYozume(Node& n) const;

  void Verbose(Node& n) const;

 private:
  std::optional<Entry> ProbeImpl(Key board_key, Hand or_hand, bool or_node) const;

  std::unordered_multimap<Key, std::pair<Hand, Entry>> entries_;
};
}  // namespace komori

#endif  // PV_TREE_HPP_
