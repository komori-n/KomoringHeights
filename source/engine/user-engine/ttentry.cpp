#include "ttentry.hpp"

namespace {
constexpr Hand kNullHand = Hand{HAND_BORROW_MASK};

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

TTEntry TTEntry::WithProofHand(std::uint32_t hash_high, Hand proof_hand) {
  TTEntry entry{};
  entry.unknown_.hash_high = hash_high;
  entry.SetProven(proof_hand);
  return entry;
}

TTEntry TTEntry::WithDisproofHand(std::uint32_t hash_high, Hand disproof_hand) {
  TTEntry entry{};
  entry.unknown_.hash_high = hash_high;
  entry.SetDisproven(disproof_hand);
  return entry;
}

bool TTEntry::IsSuperiorAndShallower(Hand hand, Depth depth) const {
  return IsUnknownNode() && hand_is_equal_or_superior(hand, unknown_.hand) && depth <= unknown_.depth;
}

bool TTEntry::IsInferiorAndShallower(Hand hand, Depth depth) const {
  return IsUnknownNode() && hand_is_equal_or_superior(unknown_.hand, hand) && depth <= unknown_.depth;
}

PnDn TTEntry::Pn() const {
  switch (Generation()) {
    case kProven:
      return 0;
    case kNonRepetitionDisproven:
    case kRepetitionDisproven:
      return kInfinitePnDn;
    default:
      return unknown_.pn;
  }
}

PnDn TTEntry::Dn() const {
  switch (Generation()) {
    case kProven:
      return kInfinitePnDn;
    case kNonRepetitionDisproven:
    case kRepetitionDisproven:
      return 0;
    default:
      return unknown_.dn;
  }
}

void TTEntry::Update(PnDn pn, PnDn dn, std::uint64_t num_searched) {
  unknown_.pn = pn;
  unknown_.dn = dn;
  unknown_.generation = CalcGeneration(num_searched);
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

bool TTEntry::UpdateWithProofHand(Hand proof_hand) {
  switch (Generation()) {
    case kProven: {
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

void TTEntry::SetProven(Hand hand) {
  if (Generation() == kProven) {
    for (auto& proven_hand : known_.hands) {
      if (proven_hand == kNullHand) {
        proven_hand = hand;
        return;
      }
    }
  } else {
    known_.generation = kProven;
    known_.hands[0] = hand;
    for (std::size_t i = 1; i < kTTEntryHandLen; ++i) {
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
    for (std::size_t i = 1; i < kTTEntryHandLen; ++i) {
      known_.hands[i] = kNullHand;
    }
  }
}

void TTEntry::SetRepetitionDisproven() {
  unknown_.pn = kInfinitePnDn;
  unknown_.dn = 0;
  unknown_.generation = kRepetitionDisproven;
}

bool TTEntry::IsFirstVisit() const {
  return Generation() == kFirstSearch;
}

void TTEntry::MarkDeleteCandidate() {
  common_.generation = kMarkDeleted;
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
      return hand;

    default:
      return hand;
  }
}  // namespace komori

bool TTEntry::IsWritableNewProofHand() const {
  return IsProvenNode() && known_.hands[kTTEntryHandLen - 1] == kNullHand;
}

bool TTEntry::IsWritableNewDisproofHand() const {
  return IsNonRepetitionDisprovenNode() && known_.hands[kTTEntryHandLen - 1] == kNullHand;
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

bool TTEntry::IsUnknownNode() const {
  return !IsProvenNode() && !IsNonRepetitionDisprovenNode();
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

TTCluster::Iterator TTCluster::LookUpWithCreation(std::uint32_t hash_high, Hand hand, Depth depth) {
  return LookUp<true>(hash_high, hand, depth);
}

TTCluster::Iterator TTCluster::LookUpWithoutCreation(std::uint32_t hash_high, Hand hand, Depth depth) {
  return LookUp<false>(hash_high, hand, depth);
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

template <bool kCreateIfNotExist>
TTCluster::Iterator TTCluster::LookUp(std::uint32_t hash_high, Hand hand, Depth depth) {
  PnDn max_pn = 1;
  PnDn max_dn = 1;
  auto end_entry = end();
  for (auto itr = LowerBound(hash_high); itr != end_entry; ++itr) {
    if (itr->HashHigh() != hash_high) {
      break;
    }

    // 完全一致するエントリが見つかった
    if (itr->ExactOrDeducable(hand, depth)) {
      return itr;
    }

    // 千日手含みの局面は優等局面の情報を使わない
    if (!itr->IsRepetitionDisprovenNode()) {
      if (itr->IsSuperiorAndShallower(hand, depth)) {
        // 優等局面よりも不詰に近いはず
        max_dn = std::max(max_dn, itr->Dn());
      }
      if (itr->IsInferiorAndShallower(hand, depth)) {
        // 劣等局面よりも詰に近いはず
        max_pn = std::max(max_pn, itr->Pn());
      }
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
  // 最も Generation が小さいエントリを消す
  auto removed_entry = std::min_element(
      begin(), end(), [](const TTEntry& lhs, const TTEntry& rhs) { return lhs.Generation() < rhs.Generation(); });

  std::move(removed_entry + 1, end(), removed_entry);
  size_--;
}

TTCluster::Iterator TTCluster::LowerBoundAll(std::uint32_t hash_high) {
  // ちょうど 7 回二分探索すれば必ず答えが見つかる
  constexpr std::size_t kLoopCnt = 7;
  static_assert(kClusterSize == 1 << kLoopCnt);

  auto curr = begin();
#define UNROLL_IMPL(i)                   \
  do {                                   \
    auto half = 1 << (kLoopCnt - 1 - i); \
    auto mid = curr + half - 1;          \
    if (mid->HashHigh() < hash_high) {   \
      curr = mid + 1;                    \
    }                                    \
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

template TTCluster::Iterator TTCluster::LookUp<false>(std::uint32_t hash_high, Hand hand, Depth depth);
template TTCluster::Iterator TTCluster::LookUp<true>(std::uint32_t hash_high, Hand hand, Depth depth);

}  // namespace komori