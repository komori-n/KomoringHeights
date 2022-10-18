#ifndef KOMORI_NEW_TT_ENTRY_HPP_
#define KOMORI_NEW_TT_ENTRY_HPP_

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mate_len.hpp"
#include "node.hpp"
#include "repetition_table.hpp"
#include "search_result.hpp"
#include "typedefs.hpp"

namespace komori {
namespace tt {
/// USI_Hash のうちどの程度を NormalTable に使用するかを示す割合。
constexpr inline double kNormalRepetitionRatio = 0.95;
/// オープンアドレス法で仕様するエントリの個数。
constexpr inline std::size_t kClusterSize = 16;
/// 探索量の上限値。オーバーフローを防ぐために 2^64-1 よりも小さい値を指定する。
constexpr inline std::uint32_t kAmountMax = std::numeric_limits<std::uint32_t>::max() / 4;
/// Hashfull（ハッシュ使用率）を計算するために仕様するエントリ数。大きすぎると探索性能が低下する。
constexpr std::size_t kHashfullCalcEntries = 10000;

/// エントリを消すしきい値。
constexpr std::size_t kGcThreshold = kClusterSize - 1;
/// GCで消すエントリ数
constexpr std::size_t kGcRemoveElementNum = 6;

// forward declaration
class TranspositionTable;

namespace detail {
class Entry {
 public:
  constexpr void Init(Key board_key, Hand hand) {
    board_key_ = board_key;
    hand_ = hand;
    vals_.may_rep = false;
    vals_.min_depth = static_cast<std::uint32_t>(kDepthMax);
    parent_board_key_ = kNullKey;
    parent_hand_ = kNullHand;
    secret_ = 0;
    for (auto& sub_entry : sub_entries_) {
      sub_entry.vals.is_used = false;
    }
  }

  constexpr bool IsFor(Key board_key) const { return board_key_ == board_key && !IsNull(); }
  constexpr bool IsFor(Key board_key, Hand hand) const { return board_key_ == board_key && hand_ == hand; }
  constexpr bool LookUp(Hand hand, Depth depth, MateLen16& len, PnDn& pn, PnDn& dn, bool& use_old_child) {
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
          if (vals_.min_depth < depth) {
            use_old_child = true;
          }
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
          if (vals_.min_depth < depth) {
            use_old_child = true;
          }

          if (len == sub_entry.len && hand == hand_) {
            return true;
          }
        }
      }
    }

    return false;
  }

  constexpr void Update(Depth depth, PnDn pn, PnDn dn, MateLen16 len, std::uint32_t amount) {
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
      auto& sub_entry = SelectRemoveEntry();
      sub_entry = {{true, amount}, len, pn, dn};
    }
  }

  constexpr Depth MinDepth() const { return static_cast<Depth>(vals_.min_depth); }
  constexpr std::pair<Key, Hand> GetParent() const { return {parent_board_key_, parent_hand_}; }
  constexpr std::uint64_t GetSecret() const { return secret_; }

  constexpr void UpdateParent(Key parent_board_key, Hand parent_hand, std::uint64_t secret) {
    parent_board_key_ = parent_board_key;
    parent_hand_ = parent_hand;
    secret_ = secret;
  }

  template <bool kIsProven>
  constexpr void Clear(Hand hand, MateLen16 len) {
    if ((kIsProven && hand_is_equal_or_superior(hand_, hand)) ||
        (!kIsProven && hand_is_equal_or_superior(hand, hand_))) {
      auto new_itr = sub_entries_.begin();
      for (auto& sub_entry : sub_entries_) {
        if (!sub_entry.vals.is_used) {
          break;
        }

        const bool is_len_superior = (kIsProven && len <= sub_entry.len) || (!kIsProven && len >= sub_entry.len);
        const bool is_equal_to_given_hand_and_len = (hand == hand_) && (len == sub_entry.len);
        const bool is_unknown = (sub_entry.pn > 0 && sub_entry.dn > 0);
        if (is_len_superior && (!is_equal_to_given_hand_and_len || is_unknown)) {
          sub_entry.vals.is_used = false;
        } else {
          if (&*new_itr != &sub_entry) {
            *new_itr = sub_entry;
            sub_entry.vals.is_used = false;
          }
          new_itr++;
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

      const auto amount = sub_entry.vals.amount;
      ret = std::min(kAmountMax, ret + amount);
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
    MateLen16 len;
    PnDn pn;
    PnDn dn;
  };

  constexpr SubEntry& SelectRemoveEntry() {
    std::uint32_t min_amount = std::numeric_limits<std::uint32_t>::max();
    SubEntry* argmin_entry = nullptr;
    for (auto& sub_entry : sub_entries_) {
      if (!sub_entry.vals.is_used) {
        return sub_entry;
      }

      if (sub_entry.vals.amount < min_amount) {
        min_amount = sub_entry.vals.amount;
        argmin_entry = &sub_entry;
      }
    }

    return *argmin_entry;
  }

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

template <typename Iterator>
inline void RemoveOne(Iterator begin, Iterator end) {
  Iterator removed = end;
  std::uint32_t removed_amount = std::numeric_limits<std::uint32_t>::max();
  for (auto itr = begin; itr != end; ++itr) {
    if (itr->IsNull()) {
      continue;
    }

    const auto amount = itr->TotalAmount();
    if (amount < removed_amount) {
      removed = itr;
      removed_amount = amount;
    }
  }

  if (removed != end) {
    removed->SetNull();
  }
}
}  // namespace detail

class Query {
 public:
  friend class TranspositionTable;

  Query() = default;
  constexpr Query(Query&&) = default;
  constexpr Query& operator=(Query&&) = default;
  ~Query() = default;

  template <typename InitialEvalFunc>
  SearchResult LookUp(bool& does_have_old_child, MateLen len, bool create_entry, InitialEvalFunc&& eval_func) {
    static_assert(std::is_invocable_v<InitialEvalFunc>);
    static_assert(std::is_same_v<std::invoke_result_t<InitialEvalFunc>, std::pair<PnDn, PnDn>>);

    MateLen16 len16 = len.To16();
    PnDn pn = 1;
    PnDn dn = 1;
    const auto begin_itr = begin();
    const auto end_itr = end();
    for (auto itr = begin_itr; itr != end_itr; ++itr) {
      if (!itr->IsFor(board_key_)) {
        continue;
      }

      const bool is_end = itr->LookUp(hand_, depth_, len16, pn, dn, does_have_old_child);
      if (is_end) {
        if (pn > 0 && dn > 0 && itr->MayRepeat() && rep_table_->Contains(path_key_)) {
          return SearchResult::MakeFinal<false, true>(itr->GetHand(), len, 1);
        }

        if (pn == 0) {
          return SearchResult::MakeFinal<true>(itr->GetHand(), MateLen::From(len16), itr->TotalAmount());
        } else if (dn == 0) {
          return SearchResult::MakeFinal<false>(itr->GetHand(), MateLen::From(len16), itr->TotalAmount());
        } else {
          // if (pn > 1000000 && dn != 0 && board_key_ == 11222989767728134593ull) {
          //   sync_cout << "info string " << board_key_ << " " << depth_ << sync_endl;
          // }

          const auto parent = itr->GetParent();
          UnknownData unknown_data{false, parent.first, parent.second, itr->GetSecret()};
          return SearchResult::MakeUnknown(pn, dn, itr->GetHand(), len, itr->TotalAmount(), unknown_data);
        }
      }
    }

    if (pn > 1000000 && dn != 0 && board_key_ == 11222989767728134593ull) {
      sync_cout << "info string ha???" << sync_endl;
    }

    const auto [init_pn, init_dn] = std::forward<InitialEvalFunc>(eval_func)();
    pn = std::max(pn, init_pn);
    dn = std::max(dn, init_dn);
    if (create_entry) {
      CreateEntry(pn, dn, len16, hand_, 1);
    }

    UnknownData unknown_data{true, kNullKey, kNullHand, 0};
    return SearchResult::MakeUnknown(pn, dn, hand_, len, 1, unknown_data);
  }

  template <typename InitialEvalFunc>
  SearchResult LookUp(MateLen len, bool create_entry, InitialEvalFunc&& eval_func) {
    bool does_have_old_child = false;
    return LookUp(does_have_old_child, len, create_entry, std::forward<InitialEvalFunc>(eval_func));
  }

  SearchResult LookUp(bool& does_have_old_child, MateLen len, bool create_entry) {
    return LookUp(does_have_old_child, len, create_entry, []() { return std::make_pair(PnDn{1}, PnDn{1}); });
  }

  SearchResult LookUp(MateLen len, bool create_entry) {
    bool does_have_old_child = false;
    return LookUp(does_have_old_child, len, create_entry);
  }

  void SetResult(const SearchResult& result) {
    if (result.IsFinal() && result.GetFinalData().is_repetition) {
      SetRepetition(result);
    } else {
      SetResultImpl(result);
      if (result.Pn() == 0) {
        CleanFinal<true>(result.GetHand(), result.Len().To16());
      } else if (result.Dn() == 0) {
        CleanFinal<false>(result.GetHand(), result.Len().To16());
      }
    }
  }

 private:
  constexpr Query(RepetitionTable& rep_table,
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
  void CleanFinal(Hand hand, MateLen16 len) {
    const auto begin_itr = begin();
    const auto end_itr = end();
    for (auto itr = begin_itr; itr != end_itr; ++itr) {
      if (itr->IsFor(board_key_)) {
        itr->Clear<kIsProven>(hand, len);
      }
    }
  }

  void SetResultImpl(const SearchResult& result) {
    if (auto itr = Find(result.GetHand())) {
      itr->Update(depth_, result.Pn(), result.Dn(), result.Len().To16(), result.Amount());
      if (!result.IsFinal()) {
        itr->UpdateParent(result.GetUnknownData().parent_board_key, result.GetUnknownData().parent_hand,
                          result.GetUnknownData().secret);
      }
    } else {
      auto new_itr = CreateEntry(result.Pn(), result.Dn(), result.Len().To16(), result.GetHand(), result.Amount());
      if (!result.IsFinal()) {
        new_itr->UpdateParent(result.GetUnknownData().parent_board_key, result.GetUnknownData().parent_hand,
                              result.GetUnknownData().secret);
      }
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

  constexpr detail::Entry* CreateEntry(PnDn pn, PnDn dn, MateLen16 len, Hand hand, std::uint32_t amount) {
    const auto begin_itr = begin();
    const auto end_itr = end();
    for (auto itr = begin_itr; itr != end_itr; ++itr) {
      if (itr->IsNull()) {
        itr->Init(board_key_, hand);
        itr->Update(depth_, pn, dn, len, amount);
        return itr;
      }
    }

    // 空きエントリがないので、amount が最も小さいエントリを消す。
    // `TotalAmount()` の計算が重いので上とは別ループにする。
    std::uint32_t min_amount = std::numeric_limits<std::uint32_t>::max();
    auto min_itr = begin_itr;
    for (auto itr = begin_itr; itr != end_itr; ++itr) {
      const auto amount = itr->TotalAmount();
      if (amount < min_amount) {
        min_amount = amount;
        min_itr = itr;
      }
    }

    min_itr->Init(board_key_, hand);
    min_itr->Update(depth_, pn, dn, len, amount);
    return min_itr;
  }

  constexpr detail::Entry* begin() { return head_entry_; }
  constexpr const detail::Entry* begin() const { return head_entry_; }
  constexpr detail::Entry* end() { return head_entry_ + kClusterSize; }
  constexpr const detail::Entry* end() const { return head_entry_ + kClusterSize; }

  RepetitionTable* rep_table_;
  detail::Entry* head_entry_;
  Key path_key_;
  Key board_key_;
  Hand hand_;
  Depth depth_;
};

class TranspositionTable {
 public:
  void Resize(std::uint64_t hash_size_mb) {
    const auto new_bytes = hash_size_mb * 1024 * 1024;
    const auto normal_bytes = static_cast<std::uint64_t>(static_cast<double>(new_bytes) * kNormalRepetitionRatio);
    const auto rep_bytes = new_bytes - normal_bytes;
    const auto new_num_entries =
        std::max(static_cast<std::uint64_t>(kClusterSize + 1), normal_bytes / sizeof(detail::Entry));
    const auto rep_num_entries = rep_bytes / 3 / sizeof(Key);

    entries_.resize(new_num_entries);
    entries_.shrink_to_fit();
    rep_table_.SetTableSizeMax(rep_num_entries);
    NewSearch();
  }

  constexpr void NewSearch() {
    for (auto& entry : entries_) {
      entry.SetNull();
    }
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
    const auto dummy_depth = kDepthMax;
    return Query{rep_table_, head_entry, kNullKey, board_key, or_hand, dummy_depth};
  }

  constexpr int Hashfull() const {
    std::size_t used = 0;

    // entries_ の最初と最後はエントリ数が若干少ないので、真ん中から kHashfullCalcEntries 個のエントリを調べる
    const std::size_t begin_idx = kClusterSize;
    const std::size_t end_idx = std::min(begin_idx + kHashfullCalcEntries, static_cast<std::size_t>(entries_.size()));

    const std::size_t num_entries = end_idx - begin_idx;
    std::size_t idx = begin_idx;
    for (std::size_t i = 0; i < num_entries; ++i) {
      if (!entries_[idx].IsNull()) {
        used++;
      }
      idx += 334;
      if (idx > end_idx) {
        idx -= end_idx - begin_idx;
      }
    }
    return static_cast<int>(used * 1000 / num_entries);
  }

  std::size_t CollectGarbage() {
    sync_cout << "info string collect garbage" << sync_endl;
    rep_table_.CollectGarbage();

    const auto removed_num = RemoveUnusedEntries();
    Compact();

    return removed_num;
  }

 private:
  constexpr detail::Entry* HeadOf(Key board_key) {
    // Stockfish の置換表と同じアイデア。少し工夫をすることで moe 演算を回避できる。
    // hash_low が [0, 2^32) の一様分布にしたがうと仮定すると、idx はだいたい [0, cluster_num) の一様分布にしたがう。
    const auto hash_low = board_key & 0xffff'ffffULL;
    auto idx = (hash_low * entries_.size()) >> 32;
    return &entries_[idx];
  }

  std::size_t RemoveUnusedEntries() {
    std::size_t removed_num = 0;

    // idx から kClusterSize 個の要素のうち使用中であるものの個数を数える
    auto count_used = [&](std::size_t idx) {
      std::size_t used = 0;
      auto end = std::min(entries_.size(), idx + kClusterSize);
      for (; idx < end; ++idx) {
        if (!entries_[idx].IsNull()) {
          ++used;
        }
      }

      return used;
    };

    std::size_t i = 0;
    std::size_t j = i + kClusterSize;
    // [i, j) の範囲で使用中のエントリの個数
    auto used_ij = count_used(0);
    std::size_t end = entries_.size();
    do {
      if (used_ij >= kGcThreshold) {
        // [i, j) の使用率が高すぎるのでエントリを適当に間引く
        for (std::size_t k = 0; k < kGcRemoveElementNum; ++k) {
          detail::RemoveOne(entries_.begin() + i, entries_.begin() + j);
        }
        i = j;
        j = i + kClusterSize;
        used_ij = count_used(i);
      } else {
        // (i, j) <-- (i + 1, j + 1) に更新する
        // しゃくとり法で used の更新をしておく
        if (!entries_[i++].IsNull()) {
          used_ij--;
        }

        if (!entries_[j++].IsNull()) {
          used_ij++;
        }
      }
    } while (j < end);

    return removed_num;
  }

  void Compact() {}

  std::vector<detail::Entry> entries_{};
  RepetitionTable rep_table_{};
};
}  // namespace tt
}  // namespace komori

#endif  // KOMORI_NEW_TT_ENTRY_HPP_
