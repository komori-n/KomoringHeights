#ifndef NEW_TT_ENTRY_HPP_
#define NEW_TT_ENTRY_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "node.hpp"
#include "typedefs.hpp"

namespace komori {
namespace tt {
/// USI_Hash のうちどの程度を NormalTable に使用するかを示す割合
constexpr inline double kNormalRepetitionRatio = 0.95;
constexpr inline std::size_t kClusterSize = 16;
constexpr inline std::uint32_t kAmountMax = std::numeric_limits<std::uint32_t>::max() / 4;

// forward declaration
class TranspositionTable;

namespace detail {
class Entry {
 public:
  constexpr void Init(Key board_key, Hand hand) {
    board_key_ = board_key;
    hand_ = hand;
    vals_.may_rep = false;
    vals_.min_depth = static_cast<std::uint32_t>(kMaxNumMateMoves);
    parent_board_key_ = kNullKey;
    parent_hand_ = kNullHand;
    secret_ = 0;
    for (auto& sub_entry : sub_entries_) {
      sub_entry.vals.is_used = false;
    }
  }

  constexpr bool IsFor(Key board_key) const { return board_key_ == board_key && !IsNull(); }
  constexpr bool IsFor(Key board_key, Hand hand) const { return board_key_ == board_key && hand_ == hand; }
  constexpr bool LookUp(Hand hand, Depth depth, MateLen& len, PnDn& pn, PnDn& dn) {
    if (hand_ == hand) {
      vals_.min_depth = std::min(vals_.min_depth, static_cast<std::uint32_t>(depth));
    }

    bool is_superior = hand_is_equal_or_superior(hand, hand_);
    bool is_inferior = hand_is_equal_or_superior(hand_, hand);
    for (auto& sub_entry : sub_entries_) {
      if (!sub_entry.vals.is_used) {
        break;
      }

      if (is_superior && len >= sub_entry.len) {
        // 現局面のほうが置換表に保存された局面より優等している
        // -> 1. 置換表で詰みが示せている（pn==0) なら現局面も詰み
        //    2. 置換表の不詰より現局面のほうが不詰を示すのが難しい
        if (sub_entry.pn == 0) {
          pn = 0;
          dn = kInfinitePnDn;
          len = sub_entry.len;
          return true;
        } else if (hand == hand_ || vals_.min_depth >= depth) {
          dn = std::max(dn, sub_entry.dn);
        }
      }
      if (is_inferior && len <= sub_entry.len) {
        // 現局面のほうが置換表に保存された局面より劣等している
        // -> 1. 置換表で不詰が示せている（dn==0) なら現局面も不詰
        //    2. 置換表の詰みより現局面のほうが詰みを示すのが難しい
        if (sub_entry.dn == 0) {
          pn = kInfinitePnDn;
          dn = 0;
          len = sub_entry.len;
          return true;
        } else if (hand == hand_ || vals_.min_depth >= depth) {
          // un に関しては sub_entry の方が条件が厳しい
          pn = std::max(pn, sub_entry.pn);
          if (len == sub_entry.len && hand == hand_) {
            return true;
          }
        }
      }
    }

    return false;
  }

  constexpr void Update(Depth depth, PnDn pn, PnDn dn, MateLen len, std::uint32_t amount) {
    vals_.min_depth = std::min(vals_.min_depth, static_cast<std::uint32_t>(depth));

    bool inserted = false;
    for (auto& sub_entry : sub_entries_) {
      if (!sub_entry.vals.is_used) {
        sub_entry = {{true, amount}, len, pn, dn};
        inserted = true;
        break;
      } else if (sub_entry.len == len) {
        sub_entry.pn = pn;
        sub_entry.dn = dn;
        sub_entry.vals.amount = amount;
        inserted = true;
        break;
      } else if ((sub_entry.pn == 0 && pn == 0 && sub_entry.len <= len) ||
                 (sub_entry.dn == 0 && dn == 0 && sub_entry.len >= len)) {
        inserted = true;
        break;
      }
    }

    if (!inserted) {
      // 適当に消す
      auto& sub_entry = sub_entries_[rand() % kSubEntryNum];
      sub_entry = {{true, amount}, len, pn, dn};
    }
  }

  constexpr std::pair<Key, Hand> GetParent() const { return {parent_board_key_, parent_hand_}; }
  constexpr std::uint64_t GetSecret() const { return secret_; }

  constexpr void UpdateParent(Key parent_board_key, Hand parent_hand, std::uint64_t secret) {
    parent_board_key_ = parent_board_key;
    parent_hand_ = parent_hand;
    secret_ = secret;
  }

  template <bool kIsProven>
  constexpr void Clear(Hand hand, MateLen len) {
    if ((kIsProven && hand_is_equal_or_superior(hand_, hand)) ||
        (!kIsProven && hand_is_equal_or_superior(hand, hand_))) {
      auto new_itr = sub_entries_.begin();
      for (auto& sub_entry : sub_entries_) {
        if (!sub_entry.vals.is_used) {
          break;
        }

        if (len >= sub_entry.len &&
            (hand != hand_ || ((kIsProven && sub_entry.pn > 0) || (!kIsProven && sub_entry.dn > 0)))) {
          sub_entry.vals.is_used = false;
        } else {
          if (&*new_itr != &sub_entry) {
            *new_itr = sub_entry;
            sub_entry.vals.is_used = false;
          }
        }
      }
    }
  }

  constexpr Hand GetHand() const { return hand_; }
  constexpr bool MayRepeat() const { return vals_.may_rep != 0; }
  constexpr void SetRepeat() {
    vals_.may_rep = 1;
    for (auto& sub_entry : sub_entries_) {
      if (!sub_entry.vals.is_used) {
        break;
      }

      if (sub_entry.pn > 0 && sub_entry.dn > 0) {
        sub_entry.pn = 1;
        sub_entry.dn = 1;
      }
    }
  }

  constexpr std::uint32_t TotalAmount() const {
    std::uint32_t ret = 0;
    for (auto& sub_entry : sub_entries_) {
      if (!sub_entry.vals.is_used) {
        break;
      }

      ret = std::min(kAmountMax, ret + sub_entry.vals.amount);
    }
    return ret;
  }

  constexpr void SetNull() { hand_ = kNullHand; }
  constexpr bool IsNull() const { return hand_ == kNullHand; }

 private:
  static constexpr inline std::size_t kSubEntryNum = 6;

  struct SubEntry {
    struct {
      std::uint32_t is_used : 1;
      std::uint32_t amount : 31;
    } vals;
    MateLen len;
    PnDn pn;
    PnDn dn;
  };

  Key board_key_;
  Key parent_board_key_{kNullKey};
  Hand hand_{kNullHand};
  Hand parent_hand_;

  std::uint64_t secret_{};
  struct {
    std::uint32_t may_rep : 1;
    std::uint32_t min_depth : 31;
  } vals_;
  std::array<SubEntry, kSubEntryNum> sub_entries_;
};

class RepetitionTable {
 public:
  static constexpr inline std::size_t kTableLen = 2;
  /// 置換表に保存された path key をすべて削除する
  void Clear() {
    for (auto& tbl : keys_) {
      tbl.clear();
    }
  }
  /// 置換表に登録してもよい key の個数を設定する
  void SetTableSizeMax(std::size_t size_max) { size_max_ = size_max; }

  /// 置換表のうち古くなった部分を削除する
  void CollectGarbage() {}

  /// `path_key` を千日手として登録する
  void Insert(Key path_key) {
    keys_[idx_].insert(path_key);
    if (keys_[idx_].size() >= size_max_ / kTableLen) {
      idx_ = (idx_ + 1) % kTableLen;
      keys_[idx_].clear();
    }
  }
  /// `path_key` が保存されていれば true
  bool Contains(Key path_key) const {
    return std::any_of(keys_.begin(), keys_.end(), [&](const auto& tbl) { return tbl.find(path_key) != tbl.end(); });
  }
  /// 現在の置換表サイズ
  constexpr std::size_t Size() const {
    std::size_t ret = 0;
    for (auto& tbl : keys_) {
      ret += tbl.size();
    }
    return ret;
  }

 private:
  std::array<std::unordered_set<Key>, kTableLen> keys_;
  std::size_t idx_{0};
  std::size_t size_max_{std::numeric_limits<std::size_t>::max()};
};
}  // namespace detail

struct SearchResult {
  PnDn pn, dn;
  Hand hand;
  MateLen len;
  bool is_repetition{false};
  bool is_first_visit{false};
  Key parent_board_key{kNullKey};
  Hand parent_hand{kNullHand};
  std::uint64_t secret{};

  constexpr PnDn Phi(bool or_node) const { return or_node ? pn : dn; }
  constexpr PnDn Delta(bool or_node) const { return or_node ? dn : pn; }
};

class Query {
 public:
  friend class TranspositionTable;

  Query() = default;
  constexpr Query(Query&&) = default;
  constexpr Query& operator=(Query&&) = default;
  ~Query() = default;

  template <typename InitialEvalFunc>
  constexpr SearchResult LookUp(MateLen len, bool create_entry, InitialEvalFunc&& eval_func) {
    static_assert(std::is_invocable_v<InitialEvalFunc>);
    static_assert(std::is_same_v<std::invoke_result_t<InitialEvalFunc>, std::pair<PnDn, PnDn>>);
    PnDn pn = 1;
    PnDn dn = 1;
    const auto begin_itr = begin();
    const auto end_itr = end();
    for (auto itr = begin_itr; itr != end_itr; ++itr) {
      if (!itr->IsFor(board_key_)) {
        continue;
      }

      const bool is_end = itr->LookUp(hand_, depth_, len, pn, dn);
      if (is_end) {
        if (pn > 0 && dn > 0 && itr->MayRepeat() && rep_table_->Contains(path_key_)) {
          return {kInfinitePnDn, 0, itr->GetHand(), len, true};
        }

        const auto parent = itr->GetParent();
        const auto secret = itr->GetSecret();
        return {pn, dn, itr->GetHand(), len, false, false, parent.first, parent.second, secret};
      }
    }

    const auto [init_pn, init_dn] = std::forward<InitialEvalFunc>(eval_func)();
    pn = std::max(pn, init_pn);
    dn = std::max(dn, init_dn);
    if (create_entry) {
      CreateEntry(pn, dn, len, hand_, 1);
    }
    return {pn, dn, hand_, len, false, true};
  }

  constexpr SearchResult LookUp(MateLen len, bool create_entry) {
    return LookUp(len, create_entry, []() { return std::make_pair(PnDn{1}, PnDn{1}); });
  }

  void SetResult(const SearchResult& result, std::uint32_t amount) {
    if (result.is_repetition) {
      SetRepetition(result);
    } else {
      SetResultImpl(result, amount);
      if (result.pn == 0) {
        CleanFinal<true>(result.hand, result.len);
      } else if (result.dn == 0) {
        CleanFinal<false>(result.hand, result.len);
      }
    }
  }

 private:
  constexpr Query(detail::RepetitionTable& rep_table,
                  detail::Entry* head_entry,
                  Key path_key,
                  Key board_key,
                  Hand hand,
                  Depth depth)
      : rep_table_{&rep_table},
        head_entry_{head_entry},
        path_key_{path_key},
        board_key_{board_key},
        hand_{hand},
        depth_{depth} {};

  void SetRepetition(const SearchResult&) {
    rep_table_->Insert(path_key_);
    if (auto itr = Find()) {
      itr->SetRepeat();
    }
  }

  template <bool kIsProven>
  void CleanFinal(Hand hand, MateLen len) {
    const auto begin_itr = begin();
    const auto end_itr = end();
    for (auto itr = begin_itr; itr != end_itr; ++itr) {
      if (itr->IsFor(board_key_)) {
        itr->Clear<kIsProven>(hand, len);
      }
    }
  }

  void SetResultImpl(const SearchResult& result, std::uint32_t amount) {
    if (auto itr = Find(result.hand)) {
      itr->Update(depth_, result.pn, result.dn, result.len, amount);
      itr->UpdateParent(result.parent_board_key, result.parent_hand, result.secret);
    } else {
      auto new_itr = CreateEntry(result.pn, result.dn, result.len, result.hand, amount);
      new_itr->UpdateParent(result.parent_board_key, result.parent_hand, result.secret);
    }
  }

  constexpr detail::Entry* Find() {
    const auto begin_itr = begin();
    const auto end_itr = end();
    for (auto itr = begin_itr; itr != end_itr; ++itr) {
      if (itr->IsFor(board_key_, hand_)) {
        return itr;
      }
    }
    return nullptr;
  }

  constexpr detail::Entry* Find(Hand hand) {
    const auto begin_itr = begin();
    const auto end_itr = end();
    for (auto itr = begin_itr; itr != end_itr; ++itr) {
      if (itr->IsFor(board_key_, hand)) {
        return itr;
      }
    }
    return nullptr;
  }

  constexpr detail::Entry* CreateEntry(PnDn pn, PnDn dn, MateLen len, Hand hand, std::uint32_t amount) {
    const auto begin_itr = begin();
    const auto end_itr = end();
    for (auto itr = begin_itr; itr != end_itr; ++itr) {
      if (itr->IsNull()) {
        itr->Init(board_key_, hand);
        itr->Update(depth_, pn, dn, len, amount);
        return itr;
      }
    }

    const auto idx = rand() & kClusterSize;
    head_entry_[idx].Init(board_key_, hand);
    head_entry_[idx].Update(depth_, pn, dn, len, amount);
    return &head_entry_[idx];
  }

  constexpr detail::Entry* begin() { return head_entry_; }
  constexpr const detail::Entry* begin() const { return head_entry_; }
  constexpr detail::Entry* end() { return head_entry_ + kClusterSize; }
  constexpr const detail::Entry* end() const { return head_entry_ + kClusterSize; }

  detail::RepetitionTable* rep_table_;
  detail::Entry* head_entry_;
  Key path_key_;
  Key board_key_;
  Hand hand_;
  Depth depth_;
};

class TranspositionTable {
 public:
  bool Resize(std::uint64_t hash_size_mb) {
    const auto new_bytes = hash_size_mb * 1024 * 1024;
    const auto normal_bytes = static_cast<std::uint64_t>(static_cast<double>(new_bytes) * kNormalRepetitionRatio);
    const auto rep_bytes = new_bytes - normal_bytes;
    const auto new_num_entries =
        std::max(static_cast<std::uint64_t>(kClusterSize + 1), normal_bytes / sizeof(detail::Entry));
    const auto rep_num_entries = rep_bytes / 3 / sizeof(Key);
    if (entries_.size() == new_num_entries) {
      return true;
    }

    entries_.resize(new_num_entries);
    entries_.shrink_to_fit();
    rep_table_.SetTableSizeMax(rep_num_entries);

    return true;
  }

  constexpr Query BuildQuery(const Node& n) {
    const auto board_key = n.Pos().state()->board_key();
    auto* const head_entry = HeadOf(board_key);

    return Query{rep_table_, head_entry, n.GetPathKey(), board_key, n.OrHand(), n.GetDepth()};
  }

  constexpr Query BuildChildQuery(const Node& n, Move move) {
    const auto board_key = n.Pos().board_key_after(move);
    auto* const head_entry = HeadOf(board_key);

    return Query{rep_table_, head_entry, n.PathKeyAfter(move), board_key, n.OrHandAfter(move), n.GetDepth() + 1};
  }

  constexpr Query BuildQueryByKey(Key board_key, Hand or_hand) {
    auto* const head_entry = HeadOf(board_key);
    const auto dummy_depth = std::numeric_limits<Depth>::max();
    return Query{rep_table_, head_entry, kNullKey, board_key, or_hand, dummy_depth};
  }

 private:
  constexpr detail::Entry* HeadOf(Key board_key) {
    // Stockfish の置換表と同じアイデア。少し工夫をすることで moe 演算を回避できる。
    // hash_low が [0, 2^32) の一様分布にしたがうと仮定すると、idx はだいたい [0, cluster_num) の一様分布にしたがう。
    const auto hash_low = board_key & 0xffff'ffffULL;
    auto idx = (hash_low * entries_.size()) >> 32;
    return &entries_[idx];
  }

  std::vector<detail::Entry> entries_{};
  detail::RepetitionTable rep_table_{};
};
}  // namespace tt
}  // namespace komori

#endif  // NEW_TT_ENTRY_HPP_