#if !defined(USER_ENGINE)
#define USER_ENGINE
#endif

#include "komoring_heights.hpp"

#include <algorithm>

#include "../../mate/mate.h"
#include "move_picker.hpp"
#include "node_travels.hpp"
#include "proof_hand.hpp"

namespace {
using komori::internal::ChildNodeCache;

constexpr std::size_t kDefaultHashSizeMb = 1024;

/// FirstSearch の初期深さ。数値実験してみた感じたと、1 ではあまり効果がなく、3 だと逆に遅くなるので
/// 2 ぐらいがちょうどよい
constexpr std::size_t kFirstSearchOrDepth = 1;
constexpr std::size_t kFirstSearchAndDepth = 2;

}  // namespace

namespace komori {
namespace internal {

template <bool kOrNode>
MoveSelector<kOrNode>::MoveSelector(const Position& n, TranspositionTable& tt, Depth depth)
    : n_{n}, tt_(tt), depth_{depth}, children_len_{0}, sum_n_{0} {
  // 各子局面の LookUp を行い、min_n の昇順になるように手を並べ替える
  auto move_picker = MovePicker<kOrNode, true>{n};
  for (const auto& move : move_picker) {
    auto& child = children_[children_len_++];
    child.move = move.move;
    child.value = move.value;

    child.query = tt.GetChildQuery<kOrNode>(n, child.move, depth_ + 1);
    auto entry = child.query.LookUpWithoutCreation();
    child.min_n = kOrNode ? entry->Pn() : entry->Dn();
    child.sum_n = kOrNode ? entry->Dn() : entry->Pn();
    child.generation = entry->StateGeneration();
    if (child.query.DoesStored(entry)) {
      child.entry = entry;
    } else {
      child.entry = nullptr;
    }

    sum_n_ = std::min(sum_n_ + child.sum_n, kInfinitePnDn);
    // 勝ちになる手があるならこれ以上調べる必要はない
    if (child.min_n == 0) {
      break;
    }
  }

  std::sort(children_.begin(), children_.begin() + children_len_,
            [this](const auto& lhs, const auto& rhs) { return Compare(lhs, rhs); });
}

template <bool kOrNode>
void MoveSelector<kOrNode>::Update(std::unordered_set<Key>& parents) {
  // 各子局面のエントリを更新する
  for (std::size_t i = 0; i < std::min(children_len_, std::size_t{2}); ++i) {
    auto& child = children_[i];
    auto* entry = child.query.RefreshWithoutCreation(child.entry);
    if (child.query.DoesStored(entry)) {
      child.entry = entry;
    } else {
      child.entry = nullptr;
    }

    auto old_sum_n = child.sum_n;
    child.min_n = kOrNode ? entry->Pn() : entry->Dn();
    child.sum_n = kOrNode ? entry->Dn() : entry->Pn();
    child.generation = entry->StateGeneration();

    sum_n_ = std::min(sum_n_ - old_sum_n + child.sum_n, kInfinitePnDn);
  }

  std::sort(children_.begin(), children_.begin() + children_len_,
            [this](const auto& lhs, const auto& rhs) { return Compare(lhs, rhs); });

  if (MinN() == 0) {
    // MinN() == 0 ではないエントリは今後使う可能性が薄いので削除候補にする
    for (std::size_t i = 1; i < children_len_; ++i) {
      auto& child = children_[i];
      auto* entry = child.entry;
      if (entry == nullptr) {
        continue;
      }

      entry = child.query.RefreshWithoutCreation(entry);
      if (entry->Pn() != 0 && entry->Dn() != 0) {
        MarkDeleteCandidates<kOrNode>(tt_, const_cast<Position&>(n_), depth_, parents, child.query, entry);
      }
    }
  }
}

template <bool kOrNode>
bool MoveSelector<kOrNode>::empty() const {
  return children_len_ == 0;
}

template <bool kOrNode>
PnDn MoveSelector<kOrNode>::Pn() const {
  return kOrNode ? MinN() : SumN();
}

template <bool kOrNode>
PnDn MoveSelector<kOrNode>::Dn() const {
  return kOrNode ? SumN() : MinN();
}

template <bool kOrNode>
bool MoveSelector<kOrNode>::IsRepetitionDisproven() const {
  if (children_len_ == 0) {
    return false;
  }

  if constexpr (kOrNode) {
    // 1 つでも千日手に向かう手があるなら、この局面はそれが原因で不詰になっているかもしれない
    return GetState(children_[children_len_ - 1].generation) == kRepetitionDisprovenState;
  } else {
    return GetState(children_[0].generation) == kRepetitionDisprovenState;
  }
}

template <bool kOrNode>
Hand MoveSelector<kOrNode>::ProofHand() const {
  if constexpr (kOrNode) {
    return BeforeHand(n_, FrontMove(), FrontHand());
  } else {
    // 子局面の証明駒の極小集合を計算する
    HandSet proof_hand = HandSet::Zero();
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = children_[i];
      proof_hand |= child.query.GetHand();
    }
    proof_hand &= n_.hand_of(~n_.side_to_move());
    return AddIfHandGivesOtherEvasions(n_, proof_hand.Get());
  }
}

template <bool kOrNode>
Hand MoveSelector<kOrNode>::DisproofHand() const {
  if constexpr (kOrNode) {
    // 子局面の反証駒の極大集合を計算する
    HandSet disproof_hand = HandSet::Full();
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = children_[i];
      disproof_hand &= BeforeHand(n_, child.move, child.query.GetHand());
    }
    disproof_hand |= n_.hand_of(n_.side_to_move());
    return RemoveIfHandGivesOtherChecks(n_, disproof_hand.Get());
  } else {
    return FrontHand();
  }
}

template <bool kOrNode>
Move MoveSelector<kOrNode>::FrontMove() const {
  // empty かどうかは呼び出し側がチェックすべきなのでここではしない
  return children_[0].move;
}

template <bool kOrNode>
TTEntry* MoveSelector<kOrNode>::FrontTTEntry() {
  // empty かどうかは呼び出し側がチェックすべきなのでここではしない
  auto& child = children_[0];
  if (child.entry == nullptr) {
    // entry がないなら作る
    child.entry = child.query.LookUpWithCreation();
  }
  return child.entry;
}

template <bool kOrNode>
const LookUpQuery& MoveSelector<kOrNode>::FrontLookUpQuery() const {
  // empty かどうかは呼び出し側がチェックすべきなのでここではしない
  return children_[0].query;
}

template <bool kOrNode>
std::pair<PnDn, PnDn> MoveSelector<kOrNode>::ChildThreshold(PnDn thpn, PnDn thdn) const {
  auto thmin_n = kOrNode ? thpn : thdn;
  auto thsum_n = kOrNode ? thdn : thpn;
  auto child_thmin_n = std::min(thmin_n, SecondMinN() + 1);
  auto child_thsum_n = std::min(thsum_n - SumNExceptFront(), kInfinitePnDn);

  return kOrNode ? std::make_pair(child_thmin_n, child_thsum_n) : std::make_pair(child_thsum_n, child_thmin_n);
}

template <bool kOrNode>
bool MoveSelector<kOrNode>::Compare(const ChildNodeCache& lhs, const ChildNodeCache& rhs) const {
  if (lhs.min_n != rhs.min_n) {
    return lhs.min_n < rhs.min_n;
  }
  if (lhs.generation != rhs.generation) {
    return lhs.generation < rhs.generation;
  }
  return lhs.value < rhs.value;
}

template <bool kOrNode>
PnDn MoveSelector<kOrNode>::MinN() const {
  return children_len_ > 0 ? children_[0].min_n : kInfinitePnDn;
}

template <bool kOrNode>
PnDn MoveSelector<kOrNode>::SumN() const {
  return sum_n_;
}

template <bool kOrNode>
PnDn MoveSelector<kOrNode>::SecondMinN() const {
  return children_len_ > 1 ? children_[1].min_n : kInfinitePnDn;
}

template <bool kOrNode>
PnDn MoveSelector<kOrNode>::SumNExceptFront() const {
  return sum_n_ - children_[0].sum_n;
}

template <bool kOrNode>
Hand MoveSelector<kOrNode>::FrontHand() const {
  auto& child = children_[0];
  if (child.entry != nullptr) {
    return child.entry->ProperHand(child.query.GetHand());
  } else {
    auto* entry = child.query.LookUpWithoutCreation();
    return entry->ProperHand(child.query.GetHand());
  }
}
}  // namespace internal

void DfPnSearcher::Init() {
  Resize(kDefaultHashSizeMb);

  or_selectors_.reserve(max_depth_ + 1);
  and_selectors_.reserve(max_depth_ + 1);
}

void DfPnSearcher::Resize(std::uint64_t size_mb) {
  tt_.Resize(size_mb);
}

bool DfPnSearcher::Search(Position& n, std::atomic_bool& stop_flag) {
  tt_.NewSearch();
  searched_node_ = 0;
  stop_ = &stop_flag;
  start_time_ = std::chrono::system_clock::now();

  auto query = tt_.GetQuery<true>(n, 0);
  auto* entry = query.LookUpWithCreation();
  std::unordered_set<Key> parents{};
  SearchImpl<true>(n, kInfinitePnDn, kInfinitePnDn, 0, parents, query, entry);

  entry = query.RefreshWithoutCreation(entry);

  // <for-debug>
  sync_cout << "info string pn=" << entry->Pn() << " dn=" << entry->Dn() << " num_searched=" << searched_node_
            << " generation=" << entry->StateGeneration() << sync_endl;  //
  // </for-debug>

  stop_ = nullptr;
  return entry->IsProvenNode();
}

Move DfPnSearcher::BestMove(const Position& n) {
  MovePicker<true> move_picker{n};
  for (auto&& move : MovePicker<true>{n}) {
    auto query = tt_.GetChildQuery<true>(n, move.move, 1);
    if (auto* entry = query.LookUpWithoutCreation(); entry->IsProvenNode()) {
      return move.move;
    }
  }

  return MOVE_NONE;
}

std::vector<Move> DfPnSearcher::BestMoves(const Position& n) {
  // 局面を書き換えるために const を外す。関数終了までに、p は n と同じ状態に戻しておかなければならない
  auto& p = const_cast<Position&>(n);
  std::unordered_map<Key, Move> memo;

  MateMovesSearch<true>(tt_, memo, p, 0);

  std::vector<Move> result;
  std::array<StateInfo, internal::kMaxNumMateMoves> st_info;
  auto st_info_p = st_info.data();
  // 探索メモをたどって詰手順を復元する
  while (memo.find(p.key()) != memo.end()) {
    auto move = memo[p.key()];
    result.push_back(move);
    p.do_move(move, *st_info_p++);

    if (result.size() >= internal::kMaxNumMateMoves) {
      break;
    }
  }

  // 動かした p をもとの n の状態に戻す
  for (auto itr = result.crbegin(); itr != result.crend(); ++itr) {
    p.undo_move(*itr);
  }

  return result;
}

template <bool kOrNode>
void DfPnSearcher::SearchImpl(Position& n,
                              PnDn thpn,
                              PnDn thdn,
                              Depth depth,
                              std::unordered_set<Key>& parents,
                              const LookUpQuery& query,
                              TTEntry* entry) {
  // 探索深さ上限 or 千日手 のときは探索を打ち切る
  if (depth + 1 > max_depth_ || parents.find(n.key()) != parents.end()) {
    entry->SetRepetitionDisproven();
    return;
  }

  // 初探索の時は n 手詰めルーチンを走らせる
  if (entry->IsFirstVisit()) {
    auto res_entry = LeafSearch<kOrNode>(tt_, n, depth, kOrNode ? kFirstSearchOrDepth : kFirstSearchAndDepth, query);
    if (res_entry->IsProvenNode() || res_entry->IsDisprovenNode()) {
      return;
    }
  }

  parents.insert(n.key());
  // スタックの消費を抑えめために、ローカル変数で確保する代わりにメンバで動的確保した領域を探索に用いる
  internal::MoveSelector<kOrNode>* selector = nullptr;
  if constexpr (kOrNode) {
    selector = &or_selectors_.emplace_back(n, tt_, depth);
  } else {
    selector = &and_selectors_.emplace_back(n, tt_, depth);
  }

  if (searched_node_ % 10'000'000 == 0) {
    tt_.Sweep();
    entry = query.RefreshWithCreation(entry);
  }

  // これ以降で return する場合、parents の復帰と selector の返却を行う必要がある。
  // これらの処理は、SEARCH_IMPL_RETURN ラベル以降で行っている。

  while (searched_node_ < max_search_node_ && !*stop_) {
    if (selector->Pn() == 0) {
      query.SetProven(selector->ProofHand());
      goto SEARCH_IMPL_RETURN;
    } else if (selector->Dn() == 0) {
      if (selector->IsRepetitionDisproven()) {
        // 千日手のため負け
        entry->SetRepetitionDisproven();
      } else {
        // 普通に詰まない
        query.SetDisproven(selector->DisproofHand());
      }
      goto SEARCH_IMPL_RETURN;
    }

    entry->Update(selector->Pn(), selector->Dn(), searched_node_);
    if (entry->Pn() >= thpn || entry->Dn() >= thdn) {
      goto SEARCH_IMPL_RETURN;
    }

    ++searched_node_;
    if (searched_node_ % 1'000'000 == 0) {
      PrintProgress(n, depth);
    }

    auto [child_thpn, child_thdn] = selector->ChildThreshold(thpn, thdn);
    auto best_move = selector->FrontMove();

    StateInfo state_info;
    n.do_move(best_move, state_info);
    SearchImpl<!kOrNode>(n, child_thpn, child_thdn, depth + 1, parents, selector->FrontLookUpQuery(),
                         selector->FrontTTEntry());
    n.undo_move(best_move);

    // GC の影響で entry の位置が変わっている場合があるのでループの最後で再取得する
    entry = query.RefreshWithCreation(entry);
    selector->Update(parents);
  }

SEARCH_IMPL_RETURN:
  // parents の復帰と selector の返却を行う必要がある
  if constexpr (kOrNode) {
    or_selectors_.pop_back();
  } else {
    and_selectors_.pop_back();
  }
  parents.erase(n.key());
}

void DfPnSearcher::PrintProgress(const Position& n, Depth depth) const {
  auto curr_time = std::chrono::system_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - start_time_).count();
  time_ms = std::max(time_ms, decltype(time_ms){1});
  auto nps = searched_node_ * 1000ULL / time_ms;
  sync_cout << "info depth " << depth << " time " << time_ms << " nodes " << searched_node_ << " nps " << nps
            << " hashfull " << tt_.Hashfull()
#if defined(KEEP_LAST_MOVE)
            << " pv " << n.moves_from_start()
#endif
            << sync_endl;
}

template void DfPnSearcher::SearchImpl<true>(Position& n,
                                             PnDn thpn,
                                             PnDn thdn,
                                             Depth depth,
                                             std::unordered_set<Key>& parents,
                                             const LookUpQuery& query,
                                             TTEntry* entry);
template void DfPnSearcher::SearchImpl<false>(Position& n,
                                              PnDn thpn,
                                              PnDn thdn,
                                              Depth depth,
                                              std::unordered_set<Key>& parents,
                                              const LookUpQuery& query,
                                              TTEntry* entry);
}  // namespace komori
