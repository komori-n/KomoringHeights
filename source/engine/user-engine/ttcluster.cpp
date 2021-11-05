#include "ttcluster.hpp"

#include "deep_dfpn.hpp"

namespace komori {
template <bool kProven>
HandsData<kProven>::HandsData(Hand hand) {
  hands_[0] = hand;
  std::fill(hands_.begin() + 1, hands_.end(), kNullHand);
}

template <bool kProven>
Hand HandsData<kProven>::ProperHand(Hand hand) const {
  for (const auto& h : hands_) {
    if (h == kNullHand) {
      break;
    }

    if ((kProven && hand_is_equal_or_superior(hand, h)) ||   // hand を証明できる
        (!kProven && hand_is_equal_or_superior(h, hand))) {  // hand を反証できる
      return h;
    }
  }
  return kNullHand;
}

template <bool kProven>
void HandsData<kProven>::Add(Hand hand) {
  for (auto& h : hands_) {
    if (h == kNullHand) {
      h = hand;
      return;
    }
  }
}

template <bool kProven>
bool HandsData<kProven>::Update(Hand hand) {
  std::size_t i = 0;
  for (auto& h : hands_) {
    if (h == kNullHand) {
      break;
    }

    if ((kProven && hand_is_equal_or_superior(h, hand)) ||   // hand で証明できる -> h はいらない
        (!kProven && hand_is_equal_or_superior(hand, h))) {  // hand で反証できる -> h はいらない
      h = kNullHand;
      continue;
    }

    // 手前から詰める
    std::swap(hands_[i], h);
    i++;
  }
  return i == 0;
}

RepetitionData::RepetitionData(Key path_key) {
  keys_[0] = path_key;
  std::fill(keys_.begin() + 1, keys_.end(), kNullKey);
}

bool RepetitionData::DoesContain(Key path_key) const {
  for (const auto& k : keys_) {
    if (k == path_key) {
      return true;
    } else if (k == kNullKey) {
      break;
    }
  }
  return false;
}

void RepetitionData::Add(Key path_key) {
  for (auto& k : keys_) {
    if (k == kNullKey) {
      k = path_key;
      return;
    }
  }
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

Hand CommonEntry::ProperHand(Hand hand) const {
  switch (GetNodeState()) {
    case NodeState::kProvenState:
      return proven_.ProperHand(hand);
    case NodeState::kDisprovenState:
      return disproven_.ProperHand(hand);
    case NodeState::kRepetitionState:
      return kNullHand;
    default:
      return unknown_.ProperHand(hand);
  }
}

void CommonEntry::UpdatePnDn(PnDn pn, PnDn dn, std::uint64_t num_searched) {
  if (auto unknown = TryGetUnknown()) {
    s_gen_.generation = CalcGeneration(num_searched);
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

TTCluster::Iterator TTCluster::SetProven(std::uint32_t hash_high, Hand proof_hand, std::uint64_t num_searched) {
  Iterator ret_entry = nullptr;
  auto top = LowerBound(hash_high);
  auto itr = top;
  auto end_entry = end();
  for (; itr != end_entry; ++itr) {
    if (itr->HashHigh() != hash_high) {
      break;
    }

    if (itr->UpdateWithProofHand(proof_hand)) {
      // itr はもういらない
    } else {
      if (auto proven = itr->TryGetProven(); ret_entry == nullptr && proven != nullptr && !proven->IsFull()) {
        // 証明済局面に空きがあるならそこに証明駒を書く
        proven->Add(proof_hand);
        ret_entry = itr;
      }
      // *top++ = *itr;
      if (top != itr) {
        *top = *itr;
      }
      top++;
    }
  }

  if (ret_entry == nullptr && top != itr) {
    // move の手間を省ける（かもしれない）ので、削除済局面があるならそこに証明済局面を上書き構築する。
    ret_entry = top;
    *top++ = CommonEntry{hash_high, num_searched, ProvenData{proof_hand}};
  }

  if (top != itr) {
    auto new_end = std::move(itr, end_entry, top);
    size_ = new_end - begin();
  }

  if (ret_entry == nullptr) {
    return Add({hash_high, num_searched, ProvenData{proof_hand}});
  }
  return ret_entry;
}

TTCluster::Iterator TTCluster::SetDisproven(std::uint32_t hash_high, Hand disproof_hand, std::uint64_t num_searched) {
  Iterator ret_entry = nullptr;
  auto top = LowerBound(hash_high);
  auto itr = top;
  auto end_entry = end();
  for (; itr != end_entry; ++itr) {
    if (itr->HashHigh() != hash_high) {
      break;
    }

    if (itr->UpdateWithDisproofHand(disproof_hand)) {
      // itr はもういらない
    } else {
      if (auto disproven = itr->TryGetDisproven();
          ret_entry == nullptr && disproven != nullptr && !disproven->IsFull()) {
        disproven->Add(disproof_hand);
        ret_entry = itr;
      }
      // *top++ = *itr;
      if (top != itr) {
        *top = *itr;
      }
      top++;
    }
  }

  if (ret_entry == nullptr && top != itr) {
    ret_entry = top;
    *top++ = CommonEntry{hash_high, num_searched, DisprovenData{disproof_hand}};
  }

  if (top != itr) {
    auto new_end = std::move(itr, end_entry, top);
    size_ = new_end - begin();
  }

  if (ret_entry == nullptr) {
    return Add({hash_high, num_searched, DisprovenData{disproof_hand}});
  }
  return ret_entry;
}

TTCluster::Iterator TTCluster::SetRepetition(std::uint32_t hash_high,
                                             Key path_key,
                                             Hand hand,
                                             std::uint64_t num_searched) {
  Iterator ret_entry = nullptr;
  auto itr = LowerBound(hash_high);
  auto end_entry = end();
  bool wrote_repetition_key = false;
  for (; itr != end_entry; ++itr) {
    if (itr->HashHigh() != hash_high) {
      break;
    }

    if (auto unknown = itr->TryGetUnknown(); unknown != nullptr && unknown->ProperHand(hand) != kNullHand) {
      // この局面は千日手かもしれないのでマークをつけておく
      itr->SetMaybeRepetition();
    } else if (auto rep = itr->TryGetRepetition(); ret_entry == nullptr && rep != nullptr && !rep->IsFull()) {
      rep->Add(path_key);
      ret_entry = itr;
    }
  }

  if (ret_entry == nullptr) {
    return Add({hash_high, num_searched, RepetitionData{path_key}});
  }
  return ret_entry;
}

template <bool kCreateIfNotExist>
TTCluster::Iterator TTCluster::LookUp(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key) {
  PnDn max_pn = InitialPnDn(depth);
  PnDn max_dn = max_pn;
  auto begin_entry = LowerBound(hash_high);
  auto end_entry = end();
  for (auto itr = begin_entry; itr != end_entry; ++itr) {
    if (itr->HashHigh() != hash_high) {
      break;
    }

    if (itr->ProperHand(hand) != kNullHand) {
      if (itr->IsMaybeRepetition()) {
        // 千日手かもしれない時は、千日手局面集をチェックする
        if (auto rep_entry = CheckRepetition(itr, hash_high, path_key); rep_entry != end()) {
          return rep_entry;
        }
      }
      if (auto unknown = itr->TryGetUnknown()) {
        // エントリの更新が可能なら最小距離をこのタイミングで更新しておく
        unknown->UpdateDepth(depth);
      }
      return itr;
    }

    // 優等局面／劣等局面の情報から (pn, dn) の初期値を引き上げる
    if (auto unknown = itr->TryGetUnknown()) {
      if (unknown->IsSuperiorThan(hand)) {
        // 現局面より itr の方が優等している -> 現局面は itr 以上に詰ますのが難しいはず
        max_pn = std::max(max_pn, unknown->Pn());
      } else if (unknown->IsInferiorThan(hand)) {
        // itr より現局面の方が優等している -> 現局面は itr 以上に不詰を示すのが難しいはず
        max_dn = std::max(max_dn, unknown->Dn());
      }
    }
  }

  if constexpr (kCreateIfNotExist) {
    return Add({hash_high, UnknownData{max_pn, max_dn, hand, depth}});
  } else {
    // エントリを新たに作るのはダメなので、適当な一時領域にデータを詰めて返す
    static CommonEntry dummy_entry;
    dummy_entry = {hash_high, UnknownData{max_pn, max_dn, hand, depth}};
    return &dummy_entry;
  }
}

TTCluster::Iterator TTCluster::CheckRepetition(Iterator begin_entry, std::uint32_t hash_high, Key path_key) {
  auto end_entry = end();
  for (auto itr = begin_entry; itr != end_entry; ++itr) {
    if (itr->HashHigh() != hash_high) {
      break;
    }

    if (auto rep = itr->TryGetRepetition(); rep != nullptr && rep->DoesContain(path_key)) {
      return itr;
    }
  }
  return end_entry;
}

TTCluster::Iterator TTCluster::Add(CommonEntry&& entry) {
  if (Size() >= kClusterSize) {
    RemoveOne();
  }

  auto insert_pos = UpperBound(entry.HashHigh());
  std::move_backward(insert_pos, end(), end() + 1);
  *insert_pos = std::move(entry);
  size_++;

  return insert_pos;
}

void TTCluster::RemoveOne() {
  // 最も必要なさそうなエントリを消す
  auto removed_entry = std::min_element(begin(), end(), [](const CommonEntry& lhs, const CommonEntry& rhs) {
    auto lstate = StripMaybeRepetition(lhs.GetNodeState());
    auto lgen = lhs.GetGeneration();
    auto rstate = StripMaybeRepetition(rhs.GetNodeState());
    auto rgen = rhs.GetGeneration();
    if (lstate != rstate) {
      return lstate < rstate;
    }
    // nodestate で決まらない場合は generation 勝負。
    return lgen < rgen;
  });

  std::move(removed_entry + 1, end(), removed_entry);
  size_--;
}

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

/// NOLINTNEXTLINE(readability-function-size)
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

/// NOLINTNEXTLINE(readability-function-size)
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

template class HandsData<false>;
template class HandsData<true>;
template TTCluster::Iterator TTCluster::LookUp<false>(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key);
template TTCluster::Iterator TTCluster::LookUp<true>(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key);
}  // namespace komori