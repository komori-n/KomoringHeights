#include "ttentry.hpp"

namespace {
constexpr Hand kNullHand = Hand{HAND_BORROW_MASK};
}  // namespace

namespace komori {
TTEntry::TTEntry(std::uint32_t hash_high, Hand hand, PnDn pn, PnDn dn, Depth depth)
    : unknown_{hash_high, hand, pn, dn, depth, kFirstSearch} {}

bool TTEntry::ExactOrDeducable(Hand hand, Depth depth) const {
  switch (Generation()) {
    case kProven:
      return DoesProve(hand);
    case kNonRepetitionDisproven:
      return DoesDisprove(hand);
    default:
      return unknown_.hand == hand && unknown_.depth == depth;
  }
}

bool TTEntry::IsSuperiorAndShallower(Hand hand, Depth depth) const {
  return hand_is_equal_or_superior(hand, unknown_.hand) && depth <= unknown_.depth;
}

bool TTEntry::IsInferiorAndShallower(Hand hand, Depth depth) const {
  return hand_is_equal_or_superior(unknown_.hand, hand) && depth <= unknown_.depth;
}

PnDn TTEntry::Pn() const {
  if (Generation() == kProven) {
    return 0;
  } else if (Generation() == kNonRepetitionDisproven || Generation() == kRepetitionDisproven) {
    return kInfinitePnDn;
  } else {
    return unknown_.pn;
  }
}

PnDn TTEntry::Dn() const {
  if (Generation() == kProven) {
    return kInfinitePnDn;
  } else if (Generation() == kNonRepetitionDisproven || Generation() == kRepetitionDisproven) {
    return 0;
  } else {
    return unknown_.dn;
  }
}

void TTEntry::Update(PnDn pn, PnDn dn, std::uint64_t num_searched) {
  if (pn == 0) {
    SetProven(unknown_.hand);
  } else if (dn == 0) {
    SetDisproven(unknown_.hand);
  } else {
    unknown_.pn = pn;
    unknown_.dn = dn;
    unknown_.generation = CalcGeneration(num_searched);
  }
}

bool TTEntry::IsProvenNode() const {
  return Generation() == kProven;
}

bool TTEntry::IsDisprovenNode() const {
  return IsNonRepetitionDisprovenNode() || IsRepetitionDisprovenNode();
}

bool TTEntry::IsNonRepetitionDisprovenNode() const {
  return Generation() == kNonRepetitionDisproven;
}

bool TTEntry::IsRepetitionDisprovenNode() const {
  return Generation() == kRepetitionDisproven;
}

bool TTEntry::IsProvenOrDisprovenNode() const {
  return IsProvenNode() || IsDisprovenNode();
}

bool TTEntry::DoesProve(Hand hand) const {
  if (!IsProvenNode()) {
    return false;
  }

  for (const auto& proof_hand : known_.hands) {
    if (proof_hand == kNullHand) {
      return false;
    }

    if (hand_is_equal_or_superior(hand, proof_hand)) {
      return true;
    }
  }

  return false;
}

bool TTEntry::DoesDisprove(Hand hand) const {
  if (!IsDisprovenNode()) {
    return false;
  }

  for (const auto& disproof_hand : known_.hands) {
    if (disproof_hand == kNullHand) {
      return false;
    }

    if (hand_is_equal_or_superior(disproof_hand, hand)) {
      return true;
    }
  }

  return false;
}

bool TTEntry::UpdateWithProofHand(Hand proof_hand) {
  switch (Generation()) {
    case kProven: {
      int idx = 0;
      // proof_hand で証明可能な手は消す。それ以外は手前から詰め直して残す。
      for (int i = 0; i < 6; ++i) {
        auto& ph = known_.hands[i];
        if (ph == kNullHand) {
          break;
        }

        if (hand_is_equal_or_superior(ph, proof_hand)) {
          ph = kNullHand;
        } else {
          if (idx != i) {
            std::swap(known_.hands[idx], ph);
          }
          idx++;
        }
      }
      return idx == 0;
    }
    case kNonRepetitionDisproven:
      return false;
    default:
      if (hand_is_equal_or_superior(unknown_.hand, proof_hand)) {
        MarkDeleteCandidate();
        return true;
      }
      return false;
  }
}

bool TTEntry::UpdateWithDisproofHand(Hand disproof_hand) {
  switch (Generation()) {
    case kProven:
      return false;
    case kNonRepetitionDisproven: {
      int idx = 0;
      for (int i = 0; i < 6; ++i) {
        auto& ph = known_.hands[i];
        if (ph == kNullHand) {
          break;
        }

        if (hand_is_equal_or_superior(disproof_hand, ph)) {
          ph = kNullHand;
        } else {
          if (idx != i) {
            std::swap(known_.hands[idx], ph);
          }
          idx++;
        }
      }
      return idx == 0;
    }
    default:
      if (hand_is_equal_or_superior(disproof_hand, unknown_.hand)) {
        MarkDeleteCandidate();
        return true;
      }
      return false;
  }
}

void TTEntry::SetProven(Hand hand) {
  if (known_.generation == kProven) {
    for (auto& proven_hand : known_.hands) {
      if (proven_hand == kNullHand) {
        proven_hand = hand;
        return;
      }
    }
  } else {
    known_.generation = kProven;
    known_.hands[0] = hand;
    for (int i = 1; i < 6; ++i) {
      known_.hands[i] = kNullHand;
    }
  }
}

void TTEntry::SetDisproven(Hand hand) {
  if (IsNonRepetitionDisprovenNode()) {
    for (auto& disproof_hand : known_.hands) {
      if (disproof_hand == kNullHand) {
        disproof_hand = hand;
        return;
      }
    }
  } else {
    known_.generation = kNonRepetitionDisproven;
    known_.hands[0] = hand;
    for (int i = 1; i < 6; ++i) {
      known_.hands[i] = kNullHand;
    }
  }
}

void TTEntry::SetRepetitionDisproven() {
  unknown_.pn = kInfinitePnDn;
  unknown_.dn = 0;
  unknown_.generation = kRepetitionDisproven;
}

void TTEntry::AddHand(Hand hand) {
  switch (Generation()) {
    case kProven:
    case kNonRepetitionDisproven:
      for (auto& h : known_.hands) {
        if (h == kNullHand) {
          h = hand;
          break;
        }
      }
  }
}

bool TTEntry::IsFirstVisit() const {
  return Generation() == kFirstSearch;
}

void TTEntry::MarkDeleteCandidate() {
  common_.generation = kMarkDeleted;
}

Hand TTEntry::FirstHand() const {
  switch (Generation()) {
    case kProven:
    case kNonRepetitionDisproven:
      return known_.hands[0];
    default:
      return unknown_.hand;
  }
}

Hand TTEntry::ProperHand(Hand hand) const {
  switch (Generation()) {
    case kProven:
      for (const auto& proof_hand : known_.hands) {
        if (proof_hand == kNullHand) {
          break;
        }
        if (hand_is_equal_or_superior(hand, proof_hand)) {
          return proof_hand;
        }
      }
      return hand;
    case kNonRepetitionDisproven:
      for (const auto& disproof_hand : known_.hands) {
        if (disproof_hand == kNullHand) {
          break;
        }
        if (hand_is_equal_or_superior(disproof_hand, hand)) {
          return disproof_hand;
        }
      }
      return known_.hands[0];
    default:
      return unknown_.hand;
  }
}  // namespace komori

bool TTEntry::IsWritableNewProofHand() const {
  return IsProvenNode() && known_.hands[5] == kNullHand;
}

bool TTEntry::IsWritableNewDisproofHand() const {
  return IsNonRepetitionDisprovenNode() && known_.hands[5] == kNullHand;
}

}  // namespace komori