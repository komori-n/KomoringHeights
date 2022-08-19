#ifndef KOMORI_TEST_LIB_HPP_
#define KOMORI_TEST_LIB_HPP_

#include <optional>

#include "../../../thread.h"
#include "../move_picker.hpp"
#include "../node.hpp"

struct TestNode {
  TestNode(const std::string& sfen, bool root_is_or_node) : n_{p_, root_is_or_node} {
    p_.set(sfen, &si_, Threads[0]);
    n_ = komori::Node{p_, root_is_or_node, 33, 4};
    mp_.emplace(n_);
  }

  komori::Node* operator->() { return &n_; }
  komori::Node& operator*() { return n_; }

  Position& Pos() { return n_.Pos(); }
  const Position& Pos() const { return n_.Pos(); }

  komori::MovePicker& MovePicker() { return *mp_; }

 private:
  Position p_;
  StateInfo si_;
  komori::Node n_;
  std::optional<komori::MovePicker> mp_;
};

template <PieceType... Pts>
inline constexpr Hand MakeHand() {
  PieceType pts[sizeof...(Pts)]{Pts...};
  Hand hand = HAND_ZERO;

  for (const auto& pt : pts) {
    add_hand(hand, pt);
  }
  return hand;
}

#endif  // KOMORI_TEST_LIB_HPP_