#include "ttcluster.hpp"

namespace komori {
namespace {
/// state の内容に沿って補正した amount を返す。例えば、ProvenState の Amount を大きくしたりする。
inline SearchedAmount GetAdjustedAmount(NodeState state, SearchedAmount amount) {
  // proven state は他のノードと比べて 5 倍探索したことにする（GCで消されづらくする）
  constexpr SearchedAmount kProvenAmountIncrease = 5;

  if (state == NodeState::kProvenState) {
    // 単純に掛け算をするとオーバーフローする可能性があるので、範囲チェックをする
    if (amount >= std::numeric_limits<SearchedAmount>::max() / kProvenAmountIncrease) {
      amount = std::numeric_limits<SearchedAmount>::max();
    } else {
      amount *= kProvenAmountIncrease;
    }
  }
  return amount;
}
}  // namespace

const CommonEntry TTCluster::kRepetitionEntry{RepetitionData{}};

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

void RepetitionCluster::Add(Key key) {
  keys_[top_] = key;
  top_++;
  if (top_ >= kMaxRepetitionClusterSize) {
    top_ = 0;
  }
}

bool RepetitionCluster::DoesContain(Key key) const {
  for (std::size_t i = 0; i < kMaxRepetitionClusterSize; ++i) {
    if (keys_[i] == key) {
      return true;
    } else if (keys_[i] == kNullKey) {
      return false;
    }
  }
  return false;
}

TTCluster::Iterator TTCluster::SetProven(std::uint32_t hash_high,
                                         Hand proof_hand,
                                         Move16 move,
                                         Depth len,
                                         SearchedAmount amount) {
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
        proven->Add(proof_hand, move, len);
        itr->UpdateSearchedAmount(amount);
        ret_entry = top;
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
    *top++ = CommonEntry{hash_high, amount, ProvenData{proof_hand, move, len}};
  }

  if (top != itr) {
    auto new_end = std::move(itr, end_entry, top);
    size_ = new_end - begin();
  }

  if (ret_entry == nullptr) {
    return Add({hash_high, amount, ProvenData{proof_hand, move, len}});
  }
  return ret_entry;
}

TTCluster::Iterator TTCluster::SetDisproven(std::uint32_t hash_high,
                                            Hand disproof_hand,
                                            Move16 move,
                                            Depth len,
                                            SearchedAmount amount) {
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
        disproven->Add(disproof_hand, move, len);
        itr->UpdateSearchedAmount(amount);
        ret_entry = top;
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
    *top++ = CommonEntry{hash_high, amount, DisprovenData{disproof_hand, move, len}};
  }

  if (top != itr) {
    auto new_end = std::move(itr, end_entry, top);
    size_ = new_end - begin();
  }

  if (ret_entry == nullptr) {
    return Add({hash_high, amount, DisprovenData{disproof_hand, move, len}});
  }
  return ret_entry;
}

TTCluster::Iterator TTCluster::SetRepetition(Iterator entry, Key path_key, SearchedAmount /* amount */) {
  rep_.Add(path_key);
  if (entry->TryGetUnknown()) {
    entry->SetMaybeRepetition();
  }
  return const_cast<TTCluster::Iterator>(&kRepetitionEntry);
}

std::size_t TTCluster::CollectGarbage(SearchedAmount th_amount) {
  auto p = begin();
  for (auto q = begin(); q != end(); ++q) {
    auto adjusted_amount = GetAdjustedAmount(q->GetNodeState(), q->GetSearchedAmount());
    if (adjusted_amount > th_amount) {
      *p++ = std::move(*q);
    }
  }

  std::size_t removed_num = end() - p;
  size_ = p - begin();

  return removed_num;
}

template <bool kCreateIfNotExist>
TTCluster::Iterator TTCluster::LookUp(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key) {
  PnDn max_pn = 1;
  PnDn max_dn = 1;
  auto begin_entry = LowerBound(hash_high);
  auto end_entry = end();
  for (auto itr = begin_entry; itr != end_entry; ++itr) {
    if (itr->HashHigh() != hash_high) {
      break;
    }

    if (itr->ProperHand(hand) != kNullHand) {
      if (itr->IsMaybeRepetition()) {
        // 千日手かもしれない時は、千日手局面集をチェックする
        if (auto rep_entry = CheckRepetition(path_key)) {
          return rep_entry;
        }
      }
      if (auto unknown = itr->TryGetUnknown()) {
        max_pn = std::max(max_pn, unknown->Pn());
        max_dn = std::max(max_dn, unknown->Dn());
        unknown->UpdatePnDn(max_pn, max_dn);

        // エントリの更新が可能なら最小距離をこのタイミングで更新しておく
        unknown->UpdateDepth(depth);
      }
      return itr;
    }

    // 優等局面／劣等局面の情報から (pn, dn) の初期値を引き上げる
    if (auto unknown = itr->TryGetUnknown(); unknown != nullptr && unknown->MinDepth() >= depth) {
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
    thread_local CommonEntry dummy_entry;
    dummy_entry = {hash_high, UnknownData{max_pn, max_dn, hand, depth}};
    return &dummy_entry;
  }
}

TTCluster::Iterator TTCluster::CheckRepetition(Key path_key) {
  if (rep_.DoesContain(path_key)) {
    return const_cast<TTCluster::Iterator>(&kRepetitionEntry);
  } else {
    return nullptr;
  }
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
    auto l_amount = GetAdjustedAmount(lhs.GetNodeState(), lhs.GetSearchedAmount());
    auto r_amount = GetAdjustedAmount(rhs.GetNodeState(), rhs.GetSearchedAmount());

    return l_amount < r_amount;
  });

  std::move(removed_entry + 1, end(), removed_entry);
  size_--;
}

template std::ostream& operator<<<false>(std::ostream& os, const HandsData<false>& data);
template std::ostream& operator<<<true>(std::ostream& os, const HandsData<true>& data);
template class HandsData<false>;
template class HandsData<true>;
template TTCluster::Iterator TTCluster::LookUp<false>(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key);
template TTCluster::Iterator TTCluster::LookUp<true>(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key);
}  // namespace komori