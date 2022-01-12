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

template class HandsData<false>;
template class HandsData<true>;
template TTCluster::Iterator TTCluster::LookUp<false>(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key);
template TTCluster::Iterator TTCluster::LookUp<true>(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key);
}  // namespace komori