#include "transposition_table.hpp"

#include "path_keys.hpp"
#include "proof_hand.hpp"
#include "ttentry.hpp"

namespace {
constexpr std::size_t kCacheLineSize = 64;
constexpr std::size_t kHashfullCalcClusters = 100;

/// val 以上の 2 の累乗数を返す
/// @caution val の最上位ビット が 0 である必要がある
template <typename T>
T RoundDownToPow2(T val) {
  T ans{1};
  while (ans <= val) {
    ans <<= 1;
  }
  return ans >> 1;
}

}  // namespace

namespace komori {
LookUpQuery::LookUpQuery(TTCluster* cluster, std::uint32_t hash_high, Hand hand, Depth depth, Key path_key)
    : cluster_{cluster}, hash_high_{hash_high}, hand_{hand}, depth_{depth}, path_key_{path_key} {}

TTEntry* LookUpQuery::LookUpWithCreation() const {
  return cluster_->LookUpWithCreation(hash_high_, hand_, depth_, path_key_);
}

TTEntry* LookUpQuery::LookUpWithoutCreation() const {
  return cluster_->LookUpWithoutCreation(hash_high_, hand_, depth_, path_key_);
}

TTEntry* LookUpQuery::RefreshWithCreation(TTEntry* entry) const {
  // 再 LookUp がサボれる場合、entry をそのまま返す
  if (IsValid(entry)) {
    return entry;
  } else {
    return LookUpWithCreation();
  }
}

TTEntry* LookUpQuery::RefreshWithoutCreation(TTEntry* entry) const {
  // 再 LookUp がサボれる場合、entry をそのまま返す
  if (IsValid(entry)) {
    return entry;
  } else {
    return LookUpWithoutCreation();
  }
}

void LookUpQuery::SetProven(Hand proof_hand, std::uint64_t num_searches) const {
  cluster_->SetProven(hash_high_, proof_hand, num_searches);
}

void LookUpQuery::SetDisproven(Hand disproof_hand, std::uint64_t num_searches) const {
  cluster_->SetDisproven(hash_high_, disproof_hand, num_searches);
}

void LookUpQuery::SetRepetition(std::uint64_t num_searches) const {
  cluster_->SetRepetition(hash_high_, path_key_, hand_, num_searches);
}

bool LookUpQuery::DoesStored(TTEntry* entry) const {
  return cluster_->DoesContain(entry);
}

bool LookUpQuery::IsValid(TTEntry* entry) const {
  return cluster_->DoesContain(entry) && hash_high_ == entry->HashHigh() &&
         ((entry->ExactOrDeducable(hand_) && !entry->IsMaybeRepetitionNode()) ||
          entry->IsRepetitionNode() && entry->CheckRepetition(path_key_));
}

TranspositionTable::TranspositionTable(void) = default;

void TranspositionTable::Resize(std::uint64_t hash_size_mb) {
  std::uint64_t new_num_clusters = hash_size_mb * 1024 * 1024 / sizeof(TTCluster);
  if (num_clusters_ == new_num_clusters) {
    return;
  }

  num_clusters_ = new_num_clusters;
  tt_raw_.resize(new_num_clusters * sizeof(TTCluster) + kCacheLineSize);
  auto tt_addr = (reinterpret_cast<std::uintptr_t>(tt_raw_.data()) + kCacheLineSize) & ~kCacheLineSize;
  tt_ = reinterpret_cast<TTCluster*>(tt_addr);

  NewSearch();
}

void TranspositionTable::NewSearch() {
  for (std::uint64_t i = 0; i < num_clusters_; ++i) {
    tt_[i].Clear();
  }
}

void TranspositionTable::Sweep() {
  for (std::uint64_t i = 0; i < num_clusters_; ++i) {
    tt_[i].Sweep();
  }
}

template <bool kOrNode>
LookUpQuery TranspositionTable::GetQuery(const Position& n, Depth depth, Key path_key) {
  Key key = n.state()->board_key();
  std::uint32_t hash_high = key >> 32;

  auto& cluster = ClusterOf(key);
  auto hand = n.hand_of(kOrNode ? n.side_to_move() : ~n.side_to_move());
  return {&cluster, hash_high, hand, depth, path_key};
}

template <bool kOrNode>
LookUpQuery TranspositionTable::GetChildQuery(const Position& n, Move move, Depth depth, Key path_key) {
  Hand hand;
  if constexpr (kOrNode) {
    hand = AfterHand(n, move, n.hand_of(n.side_to_move()));
  } else {
    hand = n.hand_of(~n.side_to_move());
  }

  Key key = n.board_key_after(move);
  std::uint32_t hash_high = key >> 32;
  auto& cluster = ClusterOf(key);

  Key path_key_after = PathKeyAfter(path_key, move, depth - 1);

  return {&cluster, hash_high, hand, depth, path_key_after};
}

int TranspositionTable::Hashfull() const {
  std::size_t used = 0;
  for (std::size_t i = 0; i < kHashfullCalcClusters; ++i) {
    used += tt_[i].Size();
  }
  return static_cast<int>(used * 1000 / kHashfullCalcClusters / TTCluster::kClusterSize);
}

TTCluster& TranspositionTable::ClusterOf(Key board_key) {
  return tt_[board_key % num_clusters_];
}

template LookUpQuery TranspositionTable::GetQuery<false>(const Position& n, Depth depth, Key path_key);
template LookUpQuery TranspositionTable::GetQuery<true>(const Position& n, Depth depth, Key path_key);
template LookUpQuery TranspositionTable::GetChildQuery<false>(const Position& n, Move move, Depth depth, Key path_key);
template LookUpQuery TranspositionTable::GetChildQuery<true>(const Position& n, Move move, Depth depth, Key path_key);
}  // namespace komori