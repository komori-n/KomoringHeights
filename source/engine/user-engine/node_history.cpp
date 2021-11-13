#include "node_history.hpp"

namespace komori {

NodeHistory::NodeState NodeHistory::State(Key board_key, Hand hand) const {
  auto [begin, end] = visited_.equal_range(board_key);
  bool is_inferior = false;
  bool is_superior = false;

  for (auto itr = begin; itr != end; ++itr) {
    auto history_hand = itr->second;
    if (hand == history_hand) {
      return NodeState::kRepetition;
    } else if (hand_is_equal_or_superior(hand, history_hand)) {
      is_superior = true;
    } else if (hand_is_equal_or_superior(history_hand, hand)) {
      is_inferior = true;
    }
  }

  if (is_superior) {
    return NodeState::kSuperior;
  } else if (is_inferior) {
    return NodeState::kInferior;
  } else {
    return NodeState::kFirst;
  }
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