#include "transposition_table.hpp"

#include "hands.hpp"
#include "node.hpp"
#include "path_keys.hpp"
#include "ttcluster.hpp"

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

CommonEntry* LookUpQuery::LookUpWithCreation() const {
  return cluster_->LookUpWithCreation(hash_high_, hand_, depth_, path_key_);
}

CommonEntry* LookUpQuery::LookUpWithoutCreation() const {
  return cluster_->LookUpWithoutCreation(hash_high_, hand_, depth_, path_key_);
}

CommonEntry* LookUpQuery::RefreshWithCreation(CommonEntry* entry) const {
  // 再 LookUp がサボれる場合、entry をそのまま返す
  if (IsValid(entry)) {
    return entry;
  } else {
    return LookUpWithCreation();
  }
}

CommonEntry* LookUpQuery::RefreshWithoutCreation(CommonEntry* entry) const {
  // 再 LookUp がサボれる場合、entry をそのまま返す
  if (IsValid(entry)) {
    return entry;
  } else {
    return LookUpWithoutCreation();
  }
}

CommonEntry* LookUpQuery::SetProven(Hand proof_hand, Move16 move, Depth len, SearchedAmount amount) const {
  return cluster_->SetProven(hash_high_, proof_hand, move, len, amount);
}

CommonEntry* LookUpQuery::SetDisproven(Hand disproof_hand, Move16 move, Depth len, SearchedAmount amount) const {
  return cluster_->SetDisproven(hash_high_, disproof_hand, move, len, amount);
}

CommonEntry* LookUpQuery::SetRepetition(CommonEntry* entry, SearchedAmount amount) const {
  return cluster_->SetRepetition(entry, path_key_, amount);
}

bool LookUpQuery::IsStored(CommonEntry* entry) const {
  return cluster_->DoesContain(entry);
}

bool LookUpQuery::IsValid(CommonEntry* entry) const {
  if (cluster_->DoesContain(entry) && hash_high_ == entry->HashHigh()) {
    if (entry->ProperHand(hand_) != kNullHand && !entry->IsMaybeRepetition()) {
      return true;
    }
    if (entry->TryGetRepetition()) {
      return true;
    }
  }
  return false;
}

TranspositionTable::TranspositionTable(int gc_hashfull) : gc_hashfull_{gc_hashfull} {};

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

std::size_t TranspositionTable::CollectGarbage() {
  std::size_t removed_num = 0;
  std::size_t obj_value = num_clusters_ * TTCluster::kClusterSize * gc_hashfull_ / 1000;

  for (;;) {
    for (std::uint64_t i = 0; i < num_clusters_; ++i) {
      removed_num += tt_[i].CollectGarbage(threshold_);
    }

    if (removed_num >= obj_value) {
      break;
    }

    threshold_++;
  }

  return removed_num;
}

LookUpQuery TranspositionTable::GetQuery(const Node& n) {
  Key key = n.Pos().state()->board_key();
  std::uint32_t hash_high = key >> 32;

  auto& cluster = ClusterOf(key);
  auto hand = n.OrHand();
  return {&cluster, hash_high, hand, n.GetDepth(), n.GetPathKey()};
}

LookUpQuery TranspositionTable::GetChildQuery(const Node& n, Move move) {
  Hand hand;
  if (n.IsOrNode()) {
    hand = AfterHand(n.Pos(), move, n.OrHand());
  } else {
    hand = n.OrHand();
  }

  Key key = n.Pos().board_key_after(move);
  std::uint32_t hash_high = key >> 32;
  auto& cluster = ClusterOf(key);

  return {&cluster, hash_high, hand, n.GetDepth() + 1, n.PathKeyAfter(move)};
}

Move TranspositionTable::LookUpBestMove(const Node& n) {
  auto query = GetQuery(n);
  auto entry = query.LookUpWithoutCreation();
  return n.Pos().to_move(entry->BestMove(n.OrHand()));
}

int TranspositionTable::Hashfull() const {
  std::size_t used = 0;
  for (std::size_t i = 0; i < kHashfullCalcClusters; ++i) {
    used += tt_[i].Size();
  }
  return static_cast<int>(used * 1000 / kHashfullCalcClusters / TTCluster::kClusterSize);
}

TranspositionTable::Stat TranspositionTable::GetStat() const {
  std::size_t used = 0;
  std::size_t proven = 0;
  std::size_t disproven = 0;
  std::size_t repetition = 0;
  std::size_t maybe_repetition = 0;
  std::size_t other = 0;
  for (std::size_t i = 0; i < kHashfullCalcClusters; ++i) {
    used += tt_[i].Size();
    for (const auto& ce : tt_[i]) {
      if (ce.GetNodeState() == NodeState::kProvenState) {
        proven++;
      } else if (ce.GetNodeState() == NodeState::kDisprovenState) {
        disproven++;
      } else if (ce.GetNodeState() == NodeState::kRepetitionState) {
        repetition++;
      } else if (ce.GetNodeState() == NodeState::kMaybeRepetitionState) {
        maybe_repetition++;
      } else {
        other++;
      }
    }
  }

  // ゼロ割対策
  used = used == 0 ? 1 : used;

  return {
      static_cast<double>(used) / kHashfullCalcClusters / TTCluster::kClusterSize,
      static_cast<double>(proven) / used,
      static_cast<double>(disproven) / used,
      static_cast<double>(repetition) / used,
      static_cast<double>(maybe_repetition) / used,
      static_cast<double>(other) / used,
  };
}

TTCluster& TranspositionTable::ClusterOf(Key board_key) {
  return tt_[board_key % num_clusters_];
}
}  // namespace komori