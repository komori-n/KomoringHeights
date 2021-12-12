#include "node_history.hpp"

namespace komori {

NodeHistory::NodeState NodeHistory::State(Key board_key, Hand hand) const {
  auto [begin, end] = visited_.equal_range(board_key);

  for (auto itr = begin; itr != end; ++itr) {
    auto history_hand = itr->second;
    if (hand_is_equal_or_superior(history_hand, hand)) {
      return NodeState::kRepetitionOrInferior;
    }
  }

  return NodeState::kFirst;
}

void NodeHistory::Visit(Key board_key, Hand hand) {
  visited_.emplace(board_key, hand);
}

void NodeHistory::Leave(Key board_key, Hand hand) {
  auto [begin, end] = visited_.equal_range(board_key);
  for (auto itr = begin; itr != end; ++itr) {
    if (itr->second == hand) {
      visited_.erase(itr);
      return;
    }
  }
}
}  // namespace komori