#include "move_selector.hpp"

#include "move_picker.hpp"
#include "node_travels.hpp"
#include "proof_hand.hpp"

namespace komori {

template <bool kOrNode>
MoveSelector<kOrNode>::MoveSelector(const Position& n, TranspositionTable& tt, Depth depth)
    : n_{n}, tt_(tt), depth_{depth}, children_len_{0}, sum_n_{0} {
  // 各子局面の LookUp を行い、min_n の昇順になるように手を並べ替える
  auto move_picker = MovePicker<kOrNode, true>{n};
  for (const auto& move : move_picker) {
    auto& child = children_[children_len_++];
    child.move = move.move;
    child.value = move.value;

    child.query = tt.GetChildQuery<kOrNode>(n, child.move, depth_ + 1);
    auto entry = child.query.LookUpWithoutCreation();
    child.min_n = kOrNode ? entry->Pn() : entry->Dn();
    child.sum_n = kOrNode ? entry->Dn() : entry->Pn();
    child.generation = entry->StateGeneration();
    if (child.query.DoesStored(entry)) {
      child.entry = entry;
    } else {
      child.entry = nullptr;
    }

    sum_n_ = std::min(sum_n_ + child.sum_n, kInfinitePnDn);
    // 勝ちになる手があるならこれ以上調べる必要はない
    if (child.min_n == 0) {
      break;
    }
  }

  std::sort(children_.begin(), children_.begin() + children_len_,
            [this](const auto& lhs, const auto& rhs) { return Compare(lhs, rhs); });
}

template <bool kOrNode>
void MoveSelector<kOrNode>::Update(std::unordered_set<Key>& parents) {
  // 各子局面のエントリを更新する
  for (std::size_t i = 0; i < std::min(children_len_, std::size_t{2}); ++i) {
    auto& child = children_[i];
    auto* entry = child.query.RefreshWithoutCreation(child.entry);
    if (child.query.DoesStored(entry)) {
      child.entry = entry;
    } else {
      child.entry = nullptr;
    }

    auto old_sum_n = child.sum_n;
    child.min_n = kOrNode ? entry->Pn() : entry->Dn();
    child.sum_n = kOrNode ? entry->Dn() : entry->Pn();
    child.generation = entry->StateGeneration();

    sum_n_ = std::min(sum_n_ - old_sum_n + child.sum_n, kInfinitePnDn);
  }

  std::sort(children_.begin(), children_.begin() + children_len_,
            [this](const auto& lhs, const auto& rhs) { return Compare(lhs, rhs); });
}

template <bool kOrNode>
bool MoveSelector<kOrNode>::empty() const {
  return children_len_ == 0;
}

template <bool kOrNode>
PnDn MoveSelector<kOrNode>::Pn() const {
  return kOrNode ? MinN() : SumN();
}

template <bool kOrNode>
PnDn MoveSelector<kOrNode>::Dn() const {
  return kOrNode ? SumN() : MinN();
}

template <bool kOrNode>
bool MoveSelector<kOrNode>::IsRepetitionDisproven() const {
  if (children_len_ == 0) {
    return false;
  }

  if constexpr (kOrNode) {
    // 1 つでも千日手に向かう手があるなら、この局面はそれが原因で不詰になっているかもしれない
    return GetState(children_[children_len_ - 1].generation) == kRepetitionDisprovenState;
  } else {
    return GetState(children_[0].generation) == kRepetitionDisprovenState;
  }
}

template <bool kOrNode>
Hand MoveSelector<kOrNode>::ProofHand() const {
  if constexpr (kOrNode) {
    return BeforeHand(n_, FrontMove(), FrontHand());
  } else {
    // 子局面の証明駒の極小集合を計算する
    HandSet proof_hand = HandSet::Zero();
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = children_[i];
      proof_hand |= child.query.GetHand();
    }
    proof_hand &= n_.hand_of(~n_.side_to_move());
    return AddIfHandGivesOtherEvasions(n_, proof_hand.Get());
  }
}

template <bool kOrNode>
Hand MoveSelector<kOrNode>::DisproofHand() const {
  if constexpr (kOrNode) {
    // 子局面の反証駒の極大集合を計算する
    HandSet disproof_hand = HandSet::Full();
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = children_[i];
      disproof_hand &= BeforeHand(n_, child.move, child.query.GetHand());
    }
    disproof_hand |= n_.hand_of(n_.side_to_move());
    return RemoveIfHandGivesOtherChecks(n_, disproof_hand.Get());
  } else {
    return FrontHand();
  }
}

template <bool kOrNode>
Move MoveSelector<kOrNode>::FrontMove() const {
  // empty かどうかは呼び出し側がチェックすべきなのでここではしない
  return children_[0].move;
}

template <bool kOrNode>
TTEntry* MoveSelector<kOrNode>::FrontTTEntry() {
  // empty かどうかは呼び出し側がチェックすべきなのでここではしない
  auto& child = children_[0];
  if (child.entry == nullptr) {
    // entry がないなら作る
    child.entry = child.query.LookUpWithCreation();
  }
  return child.entry;
}

template <bool kOrNode>
const LookUpQuery& MoveSelector<kOrNode>::FrontLookUpQuery() const {
  // empty かどうかは呼び出し側がチェックすべきなのでここではしない
  return children_[0].query;
}

template <bool kOrNode>
std::pair<PnDn, PnDn> MoveSelector<kOrNode>::ChildThreshold(PnDn thpn, PnDn thdn) const {
  auto thmin_n = kOrNode ? thpn : thdn;
  auto thsum_n = kOrNode ? thdn : thpn;
  auto child_thmin_n = std::min(thmin_n, SecondMinN() + 1);
  auto child_thsum_n = std::min(thsum_n - SumNExceptFront(), kInfinitePnDn);

  return kOrNode ? std::make_pair(child_thmin_n, child_thsum_n) : std::make_pair(child_thsum_n, child_thmin_n);
}

template <bool kOrNode>
bool MoveSelector<kOrNode>::Compare(const ChildNodeCache& lhs, const ChildNodeCache& rhs) const {
  if (lhs.min_n != rhs.min_n) {
    return lhs.min_n < rhs.min_n;
  }
  if (lhs.generation != rhs.generation) {
    return lhs.generation < rhs.generation;
  }
  return lhs.value < rhs.value;
}

template <bool kOrNode>
PnDn MoveSelector<kOrNode>::MinN() const {
  return children_len_ > 0 ? children_[0].min_n : kInfinitePnDn;
}

template <bool kOrNode>
PnDn MoveSelector<kOrNode>::SumN() const {
  return sum_n_;
}

template <bool kOrNode>
PnDn MoveSelector<kOrNode>::SecondMinN() const {
  return children_len_ > 1 ? children_[1].min_n : kInfinitePnDn;
}

template <bool kOrNode>
PnDn MoveSelector<kOrNode>::SumNExceptFront() const {
  return sum_n_ - children_[0].sum_n;
}

template <bool kOrNode>
Hand MoveSelector<kOrNode>::FrontHand() const {
  auto& child = children_[0];
  if (child.entry != nullptr) {
    return child.entry->ProperHand(child.query.GetHand());
  } else {
    auto* entry = child.query.LookUpWithoutCreation();
    return entry->ProperHand(child.query.GetHand());
  }
}

template class MoveSelector<false>;
template class MoveSelector<true>;
}  // namespace komori