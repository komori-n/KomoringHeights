#include "ttentry.hpp"

namespace komori {
TTEntry::TTEntry(std::uint32_t hash_high, ::Hand hand, PnDn pn, PnDn dn, ::Depth depth)
    : hash_high_{hash_high}, hand_{hand}, pn_{pn}, dn_{dn}, depth_{depth}, generation_{kFirstSearch} {}

bool TTEntry::ExactOrDeducable(::Hand hand, ::Depth depth) const {
  return (hand_ == hand && depth_ == depth) || DoesProve(hand) || DoesDisprove(hand);
}

bool TTEntry::IsSuperiorAndShallower(::Hand hand, ::Depth depth) const {
  return hand_is_equal_or_superior(hand, hand_) && depth <= depth_;
}

bool TTEntry::IsInferiorAndShallower(::Hand hand, ::Depth depth) const {
  return hand_is_equal_or_superior(hand_, hand) && depth <= depth_;
}

PnDn TTEntry::Pn() const {
  if (generation_ == kProven) {
    return 0;
  } else if (generation_ == kNonRepetitionDisproven || generation_ == kRepetitionDisproven) {
    return kInfinitePnDn;
  } else {
    return pn_;
  }
}

PnDn TTEntry::Dn() const {
  if (generation_ == kProven) {
    return kInfinitePnDn;
  } else if (generation_ == kNonRepetitionDisproven || generation_ == kRepetitionDisproven) {
    return 0;
  } else {
    return dn_;
  }
}

void TTEntry::Update(PnDn pn, PnDn dn, std::uint64_t num_searched) {
  if (pn == 0) {
    SetProven(hand_);
  } else if (dn == 0) {
    SetDisproven(hand_);
  } else {
    pn_ = pn;
    dn_ = dn;
    generation_ = CalcGeneration(num_searched);
  }
}

bool TTEntry::IsProvenNode() const {
  return generation_ == kProven;
}

bool TTEntry::IsDisprovenNode() const {
  return IsNonRepetitionDisprovenNode() || IsRepetitionDisprovenNode();
}

bool TTEntry::IsNonRepetitionDisprovenNode() const {
  return generation_ == kNonRepetitionDisproven;
}

bool TTEntry::IsRepetitionDisprovenNode() const {
  return generation_ == kRepetitionDisproven;
}

bool TTEntry::IsProvenOrDisprovenNode() const {
  return IsProvenNode() || IsDisprovenNode();
}

bool TTEntry::DoesProve(::Hand hand) const {
  return IsProvenNode() && hand_is_equal_or_superior(hand, hand_);
}

bool TTEntry::DoesDisprove(::Hand hand) const {
  return IsNonRepetitionDisprovenNode() && hand_is_equal_or_superior(hand_, hand);
}

void TTEntry::SetProven(::Hand hand) {
  hand_ = hand;
  pn_ = 0;
  dn_ = kInfinitePnDn;
  generation_ = kProven;
}

void TTEntry::SetDisproven(::Hand hand) {
  hand_ = hand;
  pn_ = kInfinitePnDn;
  dn_ = 0;
  generation_ = kNonRepetitionDisproven;
}

void TTEntry::SetRepetitionDisproven() {
  pn_ = kInfinitePnDn;
  dn_ = 0;
  generation_ = kRepetitionDisproven;
}

bool TTEntry::IsFirstVisit() const {
  return generation_ == kFirstSearch;
}

void TTEntry::MarkDeleteCandidate() {
  generation_ = kMarkDeleted;
}

}  // namespace komori