#include "children_cache.hpp"

#include "move_picker.hpp"
#include "node.hpp"
#include "ttcluster.hpp"

namespace komori {
namespace {
constexpr komori::StateGeneration kObviousRepetition = StateGeneration{NodeState::kRepetitionState, 0};

inline PnDn Phi(PnDn pn, PnDn dn, bool or_node) {
  return or_node ? pn : dn;
}

inline PnDn Delta(PnDn pn, PnDn dn, bool or_node) {
  return or_node ? dn : pn;
}
}  // namespace

template <bool kOrNode>
ChildrenCache::ChildrenCache(TranspositionTable& tt, const Node& n, const LookUpQuery& query, NodeTag<kOrNode>)
    : n_{n}, query_{query}, or_node_{kOrNode} {
  for (auto&& move : MovePicker{n.Pos(), NodeTag<kOrNode>{}, true}) {
    auto& child = children_[children_len_++];
    child.move = move.move;
    child.value = move.value;
    child.query = tt.GetChildQuery<kOrNode>(n, move.move);

    if (n.IsRepetitionAfter(move.move)) {
      child.pn = kInfinitePnDn;
      child.dn = 0;
      child.s_gen = kObviousRepetition;
      child.entry = nullptr;
    } else {
      child.entry = nullptr;
      child.pn = 1;
      child.dn = 1;
      child.s_gen = StateGeneration{NodeState::kOtherState, 0};
    }

    delta_ = std::min(delta_ + Delta(child.pn, child.dn, kOrNode), kInfinitePnDn);
    if (Phi(child.pn, child.dn, kOrNode) == 0 || delta_ >= kInfinitePnDn) {
      break;
    }
  }
}

CommonEntry* ChildrenCache::Update(CommonEntry* entry, std::uint64_t num_searched, std::size_t update_max_rank) {
  for (std::size_t i = 0; i < std::min(children_len_, update_max_rank); ++i) {
    auto& child = children_[i];
    if (child.s_gen != kObviousRepetition) {
      auto* child_entry = child.query.RefreshWithoutCreation(child.entry);
      if (child.query.IsStored(child_entry)) {
        child.entry = child_entry;

        if (auto unknown = child_entry->TryGetUnknown(); unknown != nullptr && unknown->IsOldChild(n_.GetDepth())) {
          does_have_old_child_ = true;
        }
      } else {
        child.entry = nullptr;
      }

      auto old_delta = Delta(child.pn, child.dn, or_node_);
      child.pn = child_entry->Pn();
      child.dn = child_entry->Dn();
      child.s_gen = child_entry->GetStateGeneration();

      delta_ = std::min(delta_ - old_delta + Delta(child.pn, child.dn, or_node_), kInfinitePnDn);
    }

    if (Phi(child.pn, child.dn, or_node_) == 0 || delta_ >= kInfinitePnDn) {
      break;
    }
  }

  std::sort(children_.begin(), children_.begin() + children_len_,
            [this](const auto& lhs, const auto& rhs) { return Compare(lhs, rhs); });

  if (delta_ == 0) {
    // 負け
    if (or_node_) {
      return SetDisproven(entry, num_searched);
    } else {
      return SetProven(entry, num_searched);
    }
  } else if (Phi(children_[0].pn, children_[0].dn, or_node_) == 0) {
    // 勝ち
    if (or_node_) {
      return SetProven(entry, num_searched);
    } else {
      return SetDisproven(entry, num_searched);
    }
  } else {
    // 勝ちでも負けでもない
    return UpdateUnknown(entry, num_searched);
  }
}

std::pair<PnDn, PnDn> ChildrenCache::ChildThreshold(PnDn thpn, PnDn thdn) const {
  auto thphi = Phi(thpn, thdn, or_node_);
  auto thdelta = Delta(thpn, thdn, or_node_);
  auto child_thphi = std::min(thphi, SecondPhi() + 1);
  auto child_thdelta = std::min(thdelta - DeltaExceptBestMove(), kInfinitePnDn);

  return or_node_ ? std::make_pair(child_thphi, child_thdelta) : std::make_pair(child_thdelta, child_thphi);
}

CommonEntry* ChildrenCache::BestMoveEntry() {
  auto& child = children_[0];
  if (child.entry == nullptr) {
    child.entry = child.query.LookUpWithCreation();
  }
  return child.entry;
}

CommonEntry* ChildrenCache::SetProven(CommonEntry* /* entry */, std::uint64_t num_searched) {
  Hand proof_hand = kNullHand;
  if (or_node_) {
    proof_hand = BeforeHand(n_.Pos(), BestMove(), BestMoveHand());
  } else {
    // 子局面の証明駒の極小集合を計算する
    HandSet set = HandSet::Zero();
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = children_[i];
      auto* entry = child.query.RefreshWithoutCreation(child.entry);
      set |= entry->ProperHand(child.query.GetHand());
    }
    proof_hand = AddIfHandGivesOtherEvasions(n_.Pos(), set.Get());
  }

  return query_.SetProven(proof_hand, num_searched);
}

CommonEntry* ChildrenCache::SetDisproven(CommonEntry* entry, std::uint64_t num_searched) {
  // children_ は千日手エントリが手前に来るようにソートされているので、以下のようにして千日手判定ができる
  if (children_len_ > 0 && children_[0].s_gen.node_state == NodeState::kRepetitionState) {
    return query_.SetRepetition(entry, num_searched);
  }

  // フツーの不詰
  Hand disproof_hand = kNullHand;
  if (or_node_) {
    // 子局面の反証駒の極大集合を計算する
    HandSet set = HandSet::Full();
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = children_[i];
      auto* entry = child.query.RefreshWithoutCreation(child.entry);
      auto hand = entry->ProperHand(child.query.GetHand());
      set &= BeforeHand(n_.Pos(), child.move, hand);
    }
    disproof_hand = RemoveIfHandGivesOtherChecks(n_.Pos(), set.Get());
  } else {
    disproof_hand = BestMoveHand();
  }

  return query_.SetDisproven(disproof_hand, num_searched);
}

CommonEntry* ChildrenCache::UpdateUnknown(CommonEntry* entry, std::uint64_t num_searched) {
  if (or_node_) {
    entry->UpdatePnDn(children_[0].pn, delta_, num_searched);
  } else {
    entry->UpdatePnDn(delta_, children_[0].dn, num_searched);
  }

  return entry;
}

Hand ChildrenCache::BestMoveHand() const {
  auto& child = children_[0];
  if (child.entry != nullptr) {
    return child.entry->ProperHand(child.query.GetHand());
  } else {
    auto* entry = child.query.LookUpWithoutCreation();
    return entry->ProperHand(child.query.GetHand());
  }
}

PnDn ChildrenCache::SecondPhi() const {
  return children_len_ > 1 ? Phi(children_[1].pn, children_[1].dn, or_node_) : kInfinitePnDn;
}

PnDn ChildrenCache::DeltaExceptBestMove() const {
  return delta_ - Delta(children_[0].pn, children_[0].dn, or_node_);
}

bool ChildrenCache::Compare(const NodeCache& lhs, const NodeCache& rhs) const {
  if (or_node_) {
    if (lhs.pn != rhs.pn) {
      return lhs.pn < rhs.pn;
    }
  } else {
    if (lhs.dn != rhs.dn) {
      return lhs.dn < rhs.dn;
    }
  }

  auto lstate = lhs.s_gen.node_state;
  auto rstate = rhs.s_gen.node_state;
  if (lstate != rstate) {
    if (or_node_) {
      return static_cast<std::uint32_t>(lstate) < static_cast<std::uint32_t>(rstate);
    } else {
      return static_cast<std::uint32_t>(lstate) > static_cast<std::uint32_t>(rstate);
    }
  }
  return lhs.value < rhs.value;
}

template ChildrenCache::ChildrenCache(TranspositionTable& tt, const Node& n, const LookUpQuery& query, NodeTag<false>);
template ChildrenCache::ChildrenCache(TranspositionTable& tt, const Node& n, const LookUpQuery& query, NodeTag<true>);
}  // namespace komori