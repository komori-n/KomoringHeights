#include "ttentry.hpp"

namespace komori {
std::ostream& operator<<(std::ostream& os, const UnknownData& data) {
  return os << "UnknownData{pn=" << ToString(data.pn_) << ", dn=" << ToString(data.dn_) << ", hand=" << data.hand_
            << ", min_depth=" << data.min_depth_ << "}";
}

template <bool kProven>
Move16 HandsData<kProven>::BestMove(Hand hand) const {
  for (const auto& e : entries_) {
    if (e.hand == kNullHand) {
      break;
    }

    if ((kProven && hand_is_equal_or_superior(hand, e.hand)) ||   // hand を証明できる
        (!kProven && hand_is_equal_or_superior(e.hand, hand))) {  // hand を反証できる
      return e.move;
    }
  }
  return MOVE_NONE;
}

template <bool kProven>
Depth HandsData<kProven>::GetSolutionLen(Hand hand) const {
  for (const auto& e : entries_) {
    if (e.hand == kNullHand) {
      break;
    }

    if ((kProven && hand_is_equal_or_superior(hand, e.hand)) ||   // hand を証明できる
        (!kProven && hand_is_equal_or_superior(e.hand, hand))) {  // hand を反証できる
      return e.len;
    }
  }
  return kMaxNumMateMoves;
}

template <bool kProven>
void HandsData<kProven>::Add(Hand hand, Move16 move, Depth len) {
  for (auto& e : entries_) {
    if (e.hand == kNullHand) {
      e = {hand, move, static_cast<std::int16_t>(len)};
      return;
    }
  }
}

template <bool kProven>
bool HandsData<kProven>::Update(Hand hand) {
  std::size_t i = 0;
  for (auto& e : entries_) {
    if (e.hand == kNullHand) {
      break;
    }

    if ((kProven && hand_is_equal_or_superior(e.hand, hand)) ||   // hand で証明できる -> h はいらない
        (!kProven && hand_is_equal_or_superior(hand, e.hand))) {  // hand で反証できる -> h はいらない
      e.hand = kNullHand;
      continue;
    }

    // 手前から詰める
    std::swap(entries_[i], e);
    i++;
  }
  return i == 0;
}

template <bool kProven>
std::ostream& operator<<(std::ostream& os, const HandsData<kProven>& data) {
  if constexpr (kProven) {
    os << "ProvenData{";
  } else {
    os << "DisprovenData{";
  }
  for (std::size_t i = 0; i < HandsData<kProven>::kHandsLen; ++i) {
    if (i != 0) {
      os << ", ";
    }
    os << data.entries_[i].hand << "/" << data.entries_[i].move << "/" << data.entries_[i].len;
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const RepetitionData& /* data */) {
  return os << "RepetitionData{}";
}

CommonEntry::CommonEntry() : dummy_{} {}

PnDn CommonEntry::Pn() const {
  switch (GetNodeState()) {
    case NodeState::kProvenState:
      return proven_.Pn();
    case NodeState::kDisprovenState:
      return disproven_.Pn();
    case NodeState::kRepetitionState:
      return rep_.Pn();
    default:
      return unknown_.Pn();
  }
}

PnDn CommonEntry::Dn() const {
  switch (GetNodeState()) {
    case NodeState::kProvenState:
      return proven_.Dn();
    case NodeState::kDisprovenState:
      return disproven_.Dn();
    case NodeState::kRepetitionState:
      return rep_.Dn();
    default:
      return unknown_.Dn();
  }
}

Move16 CommonEntry::BestMove(Hand hand) const {
  switch (GetNodeState()) {
    case NodeState::kProvenState:
      return proven_.BestMove(hand);
    case NodeState::kDisprovenState:
      return disproven_.BestMove(hand);
    default:
      return MOVE_NONE;
  }
}

Depth CommonEntry::GetSolutionLen(Hand hand) const {
  switch (GetNodeState()) {
    case NodeState::kProvenState:
      return proven_.GetSolutionLen(hand);
    case NodeState::kDisprovenState:
      return disproven_.GetSolutionLen(hand);
    default:
      return kMaxNumMateMoves;
  }
}

void CommonEntry::UpdatePnDn(PnDn pn, PnDn dn, SearchedAmount amount) {
  if (auto unknown = TryGetUnknown()) {
    if (amount >= kMinimumSearchedAmount) {
      s_amount_.amount = amount;
    }
    unknown->UpdatePnDn(pn, dn);
  }
}

bool CommonEntry::UpdateWithProofHand(Hand proof_hand) {
  switch (GetNodeState()) {
    case NodeState::kOtherState:
    case NodeState::kMaybeRepetitionState:
      // proper_hand よりたくさん持ち駒を持っているなら詰み -> もういらない
      return unknown_.IsSuperiorThan(proof_hand);
    case NodeState::kProvenState:
      return proven_.Update(proof_hand);
    default:
      return false;
  }
}

bool CommonEntry::UpdateWithDisproofHand(Hand disproof_hand) {
  switch (GetNodeState()) {
    case NodeState::kOtherState:
    case NodeState::kMaybeRepetitionState:
      // proper_hand より持ち駒が少ないなら不詰 -> もういらない
      return unknown_.IsInferiorThan(disproof_hand);
    case NodeState::kDisprovenState:
      return disproven_.Update(disproof_hand);
    default:
      return false;
  }
}

UnknownData* CommonEntry::TryGetUnknown() {
  if (GetNodeState() == NodeState::kOtherState || GetNodeState() == NodeState::kMaybeRepetitionState) {
    return &unknown_;
  }
  return nullptr;
}

ProvenData* CommonEntry::TryGetProven() {
  if (GetNodeState() == NodeState::kProvenState) {
    return &proven_;
  }
  return nullptr;
}

DisprovenData* CommonEntry::TryGetDisproven() {
  if (GetNodeState() == NodeState::kDisprovenState) {
    return &disproven_;
  }
  return nullptr;
}

RepetitionData* CommonEntry::TryGetRepetition() {
  if (GetNodeState() == NodeState::kRepetitionState) {
    return &rep_;
  }
  return nullptr;
}

std::ostream& operator<<(std::ostream& os, const CommonEntry& entry) {
  os << HexString(entry.hash_high_) << " " << entry.s_amount_.node_state << " " << entry.s_amount_.amount << " ";
  switch (entry.GetNodeState()) {
    case NodeState::kProvenState:
      return os << entry.proven_;
    case NodeState::kDisprovenState:
      return os << entry.disproven_;
    case NodeState::kRepetitionState:
      return os << entry.rep_;
    default:
      return os << entry.unknown_;
  }
}

template std::ostream& operator<<<false>(std::ostream& os, const HandsData<false>& data);
template std::ostream& operator<<<true>(std::ostream& os, const HandsData<true>& data);
template class HandsData<false>;
template class HandsData<true>;
}  // namespace komori
