#include "ttentry.hpp"

namespace {
constexpr Hand kNullHand = Hand{HAND_BORROW_MASK};
constexpr Key kNullKey = Key{0};

/// log2(val) 以上の最小の整数を返す
template <typename T>
constexpr std::size_t Log2(T val) {
  std::size_t ret = 0;
  while ((T{1} << ret) < val) {
    ret += 1;
  }
  return ret;
}
}  // namespace

namespace komori {
TTEntry::TTEntry(std::uint32_t hash_high, Hand hand, PnDn pn, PnDn dn, Depth depth)
    : unknown_{hash_high, kFirstSearch, pn, dn, hand, depth} {}

bool TTEntry::ExactOrDeducable(Hand hand, Depth depth) const {
  switch (NodeState()) {
    case kProvenState:
      return DoesProve(hand);
    case kDisprovenState:
      return DoesDisprove(hand);
    case kRepetitionState:
      return false;
    default:
      return unknown_.hand == hand && unknown_.depth == depth;
  }
}

TTEntry TTEntry::WithProofHand(std::uint32_t hash_high, Hand proof_hand) {
  TTEntry entry{};
  entry.common_.hash_high = hash_high;
  entry.SetProven(proof_hand);
  return entry;
}

TTEntry TTEntry::WithDisproofHand(std::uint32_t hash_high, Hand disproof_hand) {
  TTEntry entry{};
  entry.common_.hash_high = hash_high;
  entry.SetDisproven(disproof_hand);
  return entry;
}

TTEntry TTEntry::WithRepetitionPathKey(std::uint32_t hash_high, Key path_key) {
  TTEntry entry{};
  entry.common_.hash_high = hash_high;
  entry.SetRepetition(path_key);
  return entry;
}

bool TTEntry::IsSuperiorAndShallower(Hand hand, Depth depth) const {
  return IsUnknownNode() && hand_is_equal_or_superior(hand, unknown_.hand) && depth <= unknown_.depth;
}

bool TTEntry::IsInferiorAndShallower(Hand hand, Depth depth) const {
  return IsUnknownNode() && hand_is_equal_or_superior(unknown_.hand, hand) && depth <= unknown_.depth;
}

PnDn TTEntry::Pn() const {
  switch (NodeState()) {
    case kProvenState:
      return 0;
    case kDisprovenState:
    case kRepetitionState:
      return kInfinitePnDn;
    default:
      return unknown_.pn;
  }
}

PnDn TTEntry::Dn() const {
  switch (NodeState()) {
    case kProvenState:
      return kInfinitePnDn;
    case kDisprovenState:
    case kRepetitionState:
      return 0;
    default:
      return unknown_.dn;
  }
}

void TTEntry::Update(PnDn pn, PnDn dn, std::uint64_t num_searched) {
  unknown_.pn = pn;
  unknown_.dn = dn;
  unknown_.s_gen = MakeStateGeneration(kOtherState, CalcGeneration(num_searched));
}

bool TTEntry::IsProvenNode() const {
  return NodeState() == kProvenState;
}

bool TTEntry::IsDisprovenNode() const {
  return NodeState() == kDisprovenState;
}

bool TTEntry::IsRepetitionNode() const {
  return NodeState() == kRepetitionState;
}

bool TTEntry::IsUnknownNode() const {
  return NodeState() == kOtherState || NodeState() == kMaybeRepetitionState;
}

bool TTEntry::IsMaybeRepetitionNode() const {
  return NodeState() == kMaybeRepetitionState;
}

bool TTEntry::UpdateWithProofHand(Hand proof_hand) {
  switch (NodeState()) {
    case kProvenState: {
      std::size_t idx = 0;
      // proof_hand で証明可能な手は消す。それ以外は手前から詰め直して残す。
      for (std::size_t i = 0; i < kTTEntryHandLen; ++i) {
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

    case kDisprovenState:
    case kRepetitionState:
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
  switch (NodeState()) {
    case kProvenState:
    case kRepetitionState:
      return false;

    case kDisprovenState: {
      std::size_t idx = 0;
      for (std::size_t i = 0; i < kTTEntryHandLen; ++i) {
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

void TTEntry::UpdateWithRepetitionHand(Hand hand) {
  if (IsUnknownNode()) {
    if (unknown_.hand == hand) {
      MarkMaybeRepetition();
    }
  }
}

void TTEntry::SetProven(Hand hand) {
  if (IsProvenNode()) {
    for (auto& proven_hand : known_.hands) {
      if (proven_hand == kNullHand) {
        proven_hand = hand;
        return;
      }
    }
  } else {
    known_.s_gen = UpdateState(kProvenState, known_.s_gen);
    known_.hands[0] = hand;
    for (std::size_t i = 1; i < kTTEntryHandLen; ++i) {
      known_.hands[i] = kNullHand;
    }
  }
}

void TTEntry::SetDisproven(Hand hand) {
  if (IsDisprovenNode()) {
    for (auto& disproof_hand : known_.hands) {
      if (disproof_hand == kNullHand) {
        disproof_hand = hand;
        return;
      }
    }
  } else {
    known_.s_gen = UpdateState(kDisprovenState, known_.s_gen);
    known_.hands[0] = hand;
    for (std::size_t i = 1; i < kTTEntryHandLen; ++i) {
      known_.hands[i] = kNullHand;
    }
  }
}

void TTEntry::SetRepetition(Key path_key) {
  if (IsRepetitionNode()) {
    for (auto& p : repetition_.path_keys) {
      if (p == kNullKey) {
        p = path_key;
        return;
      }
    }
  } else {
    repetition_.s_gen = UpdateState(kRepetitionState, common_.s_gen);
    repetition_.path_keys[0] = path_key;
    for (std::size_t i = 1; i < kTTEntryPathKeyLen; ++i) {
      repetition_.path_keys[i] = kNullKey;
    }
  }
}

void TTEntry::MarkMaybeRepetition() {
  unknown_.s_gen = UpdateState(kMaybeRepetitionState, unknown_.s_gen);
  unknown_.pn = 1;
  unknown_.dn = 1;
}

bool TTEntry::IsFirstVisit() const {
  return StateGeneration() == kFirstSearch;
}

void TTEntry::MarkDeleteCandidate() {
  common_.s_gen = kMarkDeleted;
}

bool TTEntry::IsMarkedDeleteCandidate() const {
  return StateGeneration() == kMarkDeleted;
}

Hand TTEntry::ProperHand(Hand hand) const {
  switch (NodeState()) {
    case kProvenState:
      for (const auto& proof_hand : known_.hands) {
        if (proof_hand == kNullHand) {
          break;
        }
        if (hand_is_equal_or_superior(hand, proof_hand)) {
          return proof_hand;
        }
      }
      return hand;

    case kDisprovenState:
      for (const auto& disproof_hand : known_.hands) {
        if (disproof_hand == kNullHand) {
          break;
        }
        if (hand_is_equal_or_superior(disproof_hand, hand)) {
          return disproof_hand;
        }
      }
      return hand;

    default:
      return hand;
  }
}  // namespace komori

bool TTEntry::IsWritableNewProofHand() const {
  return IsProvenNode() && known_.hands[kTTEntryHandLen - 1] == kNullHand;
}

bool TTEntry::IsWritableNewDisproofHand() const {
  return IsDisprovenNode() && known_.hands[kTTEntryHandLen - 1] == kNullHand;
}

bool TTEntry::IsWritableNewRepetition() const {
  return IsRepetitionNode() && repetition_.path_keys[kTTEntryPathKeyLen - 1] == kNullKey;
}

bool TTEntry::CheckRepetition(Key path_key) const {
  if (!IsRepetitionNode()) {
    return false;
  }

  for (const auto& p : repetition_.path_keys) {
    if (p == kNullKey) {
      break;
    }

    if (p == path_key) {
      return true;
    }
  }
  return false;
}

bool TTEntry::DoesProve(Hand hand) const {
  if (!IsProvenNode()) {
    return false;
  }

  for (const auto& proof_hand : known_.hands) {
    if (proof_hand == kNullHand) {
      break;
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
      break;
    }

    if (hand_is_equal_or_superior(disproof_hand, hand)) {
      return true;
    }
  }

  return false;
}

// -----------------------------------------------------------------------------

TTCluster::Iterator TTCluster::LowerBound(std::uint32_t hash_high) {
  if (size_ == kClusterSize) {
    return LowerBoundAll(hash_high);
  } else {
    return LowerBoundPartial(hash_high);
  }
}

TTCluster::Iterator TTCluster::UpperBound(std::uint32_t hash_high) {
  auto len = Size();

  auto curr = begin();
  while (len > 0) {
    auto half = len / 2;
    auto mid = curr + half;
    if (mid->HashHigh() <= hash_high) {
      len -= half + 1;
      curr = mid + 1;
    } else {
      len = half;
    }
  }
  return curr;
}

void TTCluster::Sweep() {
  auto new_end = std::remove_if(begin(), end(), [](const TTEntry& entry) { return entry.IsMarkedDeleteCandidate(); });
  size_ = static_cast<std::size_t>(new_end - begin());
}

TTCluster::Iterator TTCluster::Add(TTEntry&& entry) {
  if (Size() >= kClusterSize) {
    RemoveLeastUsefulEntry();
  }

  auto insert_pos = UpperBound(entry.HashHigh());
  std::move_backward(insert_pos, end(), end() + 1);
  *insert_pos = std::move(entry);
  size_++;

  return insert_pos;
}

TTCluster::Iterator TTCluster::LookUpWithCreation(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key) {
  return LookUp<true>(hash_high, hand, depth, path_key);
}

TTCluster::Iterator TTCluster::LookUpWithoutCreation(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key) {
  return LookUp<false>(hash_high, hand, depth, path_key);
}

void TTCluster::SetProven(std::uint32_t hash_high, Hand proof_hand) {
  auto top = LowerBound(hash_high);
  auto itr = top;
  auto end_entry = end();
  bool wrote_proof_hand = false;
  for (; itr != end_entry; ++itr) {
    if (itr->HashHigh() != hash_high) {
      break;
    }

    if (itr->UpdateWithProofHand(proof_hand)) {
      // itr はもういらない
      // [top, end) は後で消すのでここでは何もしない
    } else {
      // itr はまだ必要
      if (!wrote_proof_hand && itr->IsWritableNewProofHand()) {
        itr->SetProven(proof_hand);
        wrote_proof_hand = true;
      }

      // *top++ = *itr
      if (top != itr) {
        *top = *itr;
      }
      *top++;
    }
  }

  if (!wrote_proof_hand && top != itr) {
    *top++ = TTEntry::WithProofHand(hash_high, proof_hand);
  }

  if (top != itr) {
    auto new_end = std::move(itr, end_entry, top);
    size_ = new_end - begin();
  }

  if (!wrote_proof_hand) {
    Add(TTEntry::WithProofHand(hash_high, proof_hand));
  }
}

void TTCluster::SetDisproven(std::uint32_t hash_high, Hand disproof_hand) {
  auto top = LowerBound(hash_high);
  auto itr = top;
  auto end_entry = end();
  bool wrote_disproof_hand = false;
  for (; itr != end_entry; ++itr) {
    if (itr->HashHigh() != hash_high) {
      break;
    }

    if (itr->UpdateWithDisproofHand(disproof_hand)) {
      // itr はもういらない
    } else {
      // itr はまだ必要
      if (!wrote_disproof_hand && itr->IsWritableNewDisproofHand()) {
        itr->SetDisproven(disproof_hand);
        wrote_disproof_hand = true;
      }

      if (top != itr) {
        *top = *itr;
      }
      *top++;
    }
  }

  if (!wrote_disproof_hand && top != itr) {
    *top++ = TTEntry::WithDisproofHand(hash_high, disproof_hand);
  }

  if (top != itr) {
    auto new_end = std::move(itr, end_entry, top);
    size_ = new_end - begin();
  }

  if (!wrote_disproof_hand) {
    Add(TTEntry::WithDisproofHand(hash_high, disproof_hand));
  }
}

void TTCluster::SetRepetition(std::uint32_t hash_high, Key path_key, Hand hand) {
  auto itr = LowerBound(hash_high);
  auto end_entry = end();
  bool wrote_repetition_key = false;
  for (; itr != end_entry; ++itr) {
    if (itr->HashHigh() != hash_high) {
      break;
    }

    itr->UpdateWithRepetitionHand(hand);

    if (!wrote_repetition_key && itr->IsWritableNewRepetition()) {
      itr->SetRepetition(path_key);
      wrote_repetition_key = true;
    }
  }

  if (!wrote_repetition_key) {
    Add(TTEntry::WithRepetitionPathKey(hash_high, path_key));
  }
}

template <bool kCreateIfNotExist>
TTCluster::Iterator TTCluster::LookUp(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key) {
  PnDn max_pn = 1;
  PnDn max_dn = 1;
  auto end_entry = end();
  for (auto itr = LowerBound(hash_high); itr != end_entry; ++itr) {
    if (itr->HashHigh() != hash_high) {
      break;
    }

    // 完全一致するエントリが見つかった
    if (itr->ExactOrDeducable(hand, depth)) {
      if (itr->IsMaybeRepetitionNode()) {
        for (auto itr2 = LowerBound(hash_high); itr2 != end_entry; ++itr2) {
          if (itr2->HashHigh() != hash_high) {
            break;
          }

          if (itr2->CheckRepetition(path_key)) {
            return itr2;
          }
        }
      }
      return itr;
    }

    if (itr->IsSuperiorAndShallower(hand, depth)) {
      // 優等局面よりも不詰に近いはず
      max_dn = std::max(max_dn, itr->Dn());
    }
    if (itr->IsInferiorAndShallower(hand, depth)) {
      // 劣等局面よりも詰に近いはず
      max_pn = std::max(max_pn, itr->Pn());
    }
  }

  if constexpr (kCreateIfNotExist) {
    return Add({hash_high, hand, max_pn, max_dn, depth});
  } else {
    static TTEntry dummy_entry;
    dummy_entry = {hash_high, hand, max_pn, max_dn, depth};
    return &dummy_entry;
  }
}

void TTCluster::RemoveLeastUsefulEntry() {
  auto old_size = Size();
  Sweep();
  if (Size() != old_size) {
    return;
  }

  // 最も Generation が小さいエントリを消す
  auto removed_entry = std::min_element(begin(), end(), [](const TTEntry& lhs, const TTEntry& rhs) {
    return lhs.StateGeneration() < rhs.StateGeneration();
  });

  std::move(removed_entry + 1, end(), removed_entry);
  size_--;
}

TTCluster::Iterator TTCluster::LowerBoundAll(std::uint32_t hash_high) {
  // ちょうど 7 回二分探索すれば必ず答えが見つかる
  constexpr std::size_t kLoopCnt = 7;
  static_assert(kClusterSize == 1 << kLoopCnt);

  auto curr = begin();
#define UNROLL_IMPL(i)                     \
  do {                                     \
    auto half = 1 << (kLoopCnt - 1 - (i)); \
    auto mid = curr + half - 1;            \
    if (mid->HashHigh() < hash_high) {     \
      curr = mid + 1;                      \
    }                                      \
  } while (false)

  UNROLL_IMPL(0);
  UNROLL_IMPL(1);
  UNROLL_IMPL(2);
  UNROLL_IMPL(3);
  UNROLL_IMPL(4);
  UNROLL_IMPL(5);
  UNROLL_IMPL(6);

#undef UNROLL_IMPL

  return curr;
}

TTCluster::Iterator TTCluster::LowerBoundPartial(std::uint32_t hash_high) {
  auto len = Size();

  auto curr = begin();
#define UNROLL_IMPL()                  \
  do {                                 \
    if (len == 0) {                    \
      return curr;                     \
    }                                  \
                                       \
    auto half = len / 2;               \
    auto mid = curr + half;            \
    if (mid->HashHigh() < hash_high) { \
      len -= half + 1;                 \
      curr = mid + 1;                  \
    } else {                           \
      len = half;                      \
    }                                  \
  } while (false)

  // 高々 8 回二分探索すれば必ず答えが見つかる
  static_assert(kClusterSize < (1 << 8));
  UNROLL_IMPL();
  UNROLL_IMPL();
  UNROLL_IMPL();
  UNROLL_IMPL();
  UNROLL_IMPL();
  UNROLL_IMPL();
  UNROLL_IMPL();

#undef UNROLL_IMPL

  return curr;
}

template TTCluster::Iterator TTCluster::LookUp<false>(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key);
template TTCluster::Iterator TTCluster::LookUp<true>(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key);

}  // namespace komori