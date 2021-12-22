#include "children_cache.hpp"

#include <numeric>

#include "../../mate/mate.h"
#include "move_picker.hpp"
#include "node.hpp"
#include "ttcluster.hpp"

namespace komori {
namespace {
inline PnDn Phi(PnDn pn, PnDn dn, bool or_node) {
  return or_node ? pn : dn;
}

inline PnDn Delta(PnDn pn, PnDn dn, bool or_node) {
  return or_node ? dn : pn;
}

/**
 * @brief 1手詰めルーチン。詰む場合は証明駒を返す。
 *
 * @param n  現局面（OrNode）
 * @return Hand （詰む場合）証明駒／（詰まない場合）kNullHand
 */
inline Hand CheckMate1Ply(Node& n) {
  if (!n.Pos().in_check()) {
    if (auto move = Mate::mate_1ply(n.Pos()); move != MOVE_NONE) {
      n.DoMove(move);
      auto hand = AddIfHandGivesOtherEvasions(n.Pos(), HAND_ZERO);
      n.UndoMove(move);

      return BeforeHand(n.Pos(), move, hand);
    }
  }
  return kNullHand;
}

/**
 * @brief 王手がある可能性があるなら true。どう考えても王手できないなら false。
 *
 * この関数の戻り値が true であっても、合法王手が存在しない可能性がある。
 *
 * @param n   現局面（OrNode）
 * @return true（詰む場合）／false（詰まない場合）
 */
inline bool DoesHaveMatePossibility(const Position& n) {
  auto us = n.side_to_move();
  auto them = ~us;
  auto hand = n.hand_of(us);
  auto king_sq = n.king_square(them);

  auto droppable_bb = ~n.pieces();
  for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
    if (hand_exists(hand, pr)) {
      // 二歩チェック
      if (pr == PAWN && (n.pieces(us, PAWN) & FILE_BB[file_of(king_sq)])) {
        continue;
      }

      if (droppable_bb.test(StepEffect(pr, them, king_sq))) {
        // pr を持っていたら王手ができる
        return true;
      }
    }
  }

  auto x =
      ((n.pieces(PAWN) & check_candidate_bb(us, PAWN, king_sq)) |
       (n.pieces(LANCE) & check_candidate_bb(us, LANCE, king_sq)) |
       (n.pieces(KNIGHT) & check_candidate_bb(us, KNIGHT, king_sq)) |
       (n.pieces(SILVER) & check_candidate_bb(us, SILVER, king_sq)) |
       (n.pieces(GOLDS) & check_candidate_bb(us, GOLD, king_sq)) |
       (n.pieces(BISHOP) & check_candidate_bb(us, BISHOP, king_sq)) |
       (n.pieces(ROOK_DRAGON)) |                                  // ROOK,DRAGONは無条件全域
       (n.pieces(HORSE) & check_candidate_bb(us, ROOK, king_sq))  // check_candidate_bbにはROOKと書いてるけど、HORSE
       ) &
      n.pieces(us);
  auto y = n.blockers_for_king(them) & n.pieces(us);

  return x | y;
}
}  // namespace

ChildrenCache::NodeCache ChildrenCache::NodeCache::FromRepetitionMove(ExtMove move, Hand hand) {
  NodeCache cache;
  cache.move = move;

  cache.entry = nullptr;
  cache.search_result = {NodeState::kRepetitionState, kInfinitePnDn, 0, hand};
  cache.is_first = false;
  cache.depth = Depth{kMaxNumMateMoves};

  return cache;
}

ChildrenCache::NodeCache ChildrenCache::NodeCache::FromUnknownMove(LookUpQuery&& query, ExtMove move, Hand hand) {
  NodeCache cache;
  cache.move = move;
  cache.query = std::move(query);
  auto* entry = cache.query.LookUpWithoutCreation();
  cache.entry = entry;
  cache.search_result = {*entry, hand};
  cache.is_first = entry->IsFirstVisit();
  if (auto unknown = entry->TryGetUnknown()) {
    cache.depth = unknown->MinDepth();
  } else {
    cache.depth = Depth{kMaxNumMateMoves};
  }

  return cache;
}

template <bool kOrNode>
ChildrenCache::ChildrenCache(TranspositionTable& tt, const Node& n, bool first_search, NodeTag<kOrNode>)
    : or_node_{kOrNode} {
  // DoMove() や UndoMove() をしたいので const を外す
  Node& nn = const_cast<Node&>(n);

  // 1 手詰めの場合、指し手生成をサボることができる
  // が、AndNode の 2 手詰めルーチンで mate_1ply を呼ぶのでここでやっても意味がない

  for (auto&& move : MovePicker{nn.Pos(), NodeTag<kOrNode>{}, true}) {
    auto& curr_idx = idx_[children_len_] = children_len_;
    auto& child = children_[children_len_++];
    if (nn.IsRepetitionAfter(move.move)) {
      child = NodeCache::FromRepetitionMove(move, nn.OrHand());
    } else {
      auto&& query = tt.GetChildQuery<kOrNode>(nn, move.move);
      auto hand = kOrNode ? AfterHand(nn.Pos(), move.move, nn.OrHand()) : nn.OrHand();
      child = NodeCache::FromUnknownMove(std::move(query), move, hand);
      if (child.depth < nn.GetDepth()) {
        does_have_old_child_ = true;
      }

      // 受け方の first search の場合、1手掘り進めてみる
      if (!kOrNode && first_search && child.is_first) {
        nn.DoMove(move.move);
        // 1手詰めチェック
        if (auto proof_hand = CheckMate1Ply(nn); proof_hand != kNullHand) {
          // move を選ぶと1手詰み。
          SearchResult dummy_entry = {NodeState::kProvenState, 0, kInfinitePnDn, proof_hand};

          // Update 時に delta の差分更新を行うので、初回だけ delta を補正しておく必要がある
          delta_ += child.Delta(kOrNode);
          UpdateNthChildWithoutSort(curr_idx, dummy_entry, nn.GetMoveCount());
          nn.UndoMove(move.move);
          continue;
        }

        // 1手不詰チェック
        // 一見重そうな処理だが、実験したところここの if 文（これ以上王手ができるどうかの判定} を入れたほうが
        // 結果として探索が高速化される。
        if (!DoesHaveMatePossibility(n.Pos())) {
          auto hand2 = RemoveIfHandGivesOtherChecks(nn.Pos(), Hand{HAND_BIT_MASK});
          child.search_result = {NodeState::kDisprovenState, kInfinitePnDn, 0, hand2};
        }
        nn.UndoMove(move.move);
      }
    }

    delta_ = Clamp(delta_ + child.Delta(kOrNode));
    if (child.Phi(kOrNode) == 0) {
      break;
    }
  }

  std::sort(idx_.begin(), idx_.begin() + children_len_,
            [this](const auto& lhs, const auto& rhs) { return Compare(children_[lhs], children_[rhs]); });
}

void ChildrenCache::UpdateFront(const SearchResult& search_result, std::uint64_t move_count) {
  UpdateNthChildWithoutSort(0, search_result, move_count);

  // idx=0 の更新を受けて子ノードをソートし直す
  // [1, n) はソート済なので、insertion sort で高速にソートできる
  std::size_t j = 0 + 1;
  while (j < children_len_ && Compare(NthChild(j), NthChild(0))) {
    ++j;
  }
  std::rotate(idx_.begin(), idx_.begin() + 1, idx_.begin() + j);
}

SearchResult ChildrenCache::CurrentResult(const Node& n) const {
  if (delta_ == 0) {
    if (or_node_) {
      return GetDisprovenResult(n);
    } else {
      return GetProvenResult(n);
    }
  } else if (NthChild(0).Phi(or_node_) == 0) {
    if (or_node_) {
      return GetProvenResult(n);
    } else {
      return GetDisprovenResult(n);
    }
  } else {
    return GetUnknownResult(n);
  }
}

std::pair<PnDn, PnDn> ChildrenCache::ChildThreshold(PnDn thpn, PnDn thdn) const {
  auto thphi = Phi(thpn, thdn, or_node_);
  auto thdelta = Delta(thpn, thdn, or_node_);
  auto child_thphi = Clamp(thphi, 0, SecondPhi() + 1);
  auto child_thdelta = Clamp(thdelta - DeltaExceptBestMove());

  return or_node_ ? std::make_pair(child_thphi, child_thdelta) : std::make_pair(child_thdelta, child_thphi);
}

void ChildrenCache::UpdateNthChildWithoutSort(std::size_t i,
                                              const SearchResult& search_result,
                                              std::uint64_t num_searches) {
  auto& child = NthChild(i);
  // delta_ を差分更新する。
  auto old_delta = child.Delta(or_node_);
  child.is_first = false;
  child.search_result = search_result;
  delta_ = Clamp(delta_ - old_delta + child.Delta(or_node_));

  switch (search_result.GetNodeState()) {
    case NodeState::kProvenState:
      child.query.SetProven(search_result.ProperHand(), num_searches);
      break;
    case NodeState::kDisprovenState:
      child.query.SetDisproven(search_result.ProperHand(), num_searches);
      break;
    case NodeState::kRepetitionState:
      child.entry = child.query.RefreshWithCreation(child.entry);
      child.query.SetRepetition(child.entry, num_searches);
      break;
    default:
      child.entry = child.query.RefreshWithCreation(child.entry);
      child.entry->UpdatePnDn(search_result.Pn(), search_result.Dn(), num_searches);
  }
}

SearchResult ChildrenCache::GetProvenResult(const Node& n) const {
  Hand proof_hand = kNullHand;
  if (or_node_) {
    auto& best_child = NthChild(0);
    proof_hand = BeforeHand(n.Pos(), best_child.move, best_child.search_result.ProperHand());
  } else {
    // 子局面の証明駒の極小集合を計算する
    HandSet set = HandSet::Zero();
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = NthChild(i);
      set |= child.search_result.ProperHand();
    }
    proof_hand = AddIfHandGivesOtherEvasions(n.Pos(), set.Get());
  }

  return {NodeState::kProvenState, 0, kInfinitePnDn, proof_hand};
}

SearchResult ChildrenCache::GetDisprovenResult(const Node& n) const {
  // children_ は千日手エントリが手前に来るようにソートされているので、以下のようにして千日手判定ができる
  if (children_len_ > 0 && NthChild(0).search_result.GetNodeState() == NodeState::kRepetitionState) {
    return {NodeState::kRepetitionState, kInfinitePnDn, 0, n.OrHand()};
  }

  // フツーの不詰
  Hand disproof_hand = kNullHand;
  if (or_node_) {
    // 子局面の反証駒の極大集合を計算する
    HandSet set = HandSet::Full();
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = NthChild(i);
      set &= BeforeHand(n.Pos(), child.move, child.search_result.ProperHand());
    }
    disproof_hand = RemoveIfHandGivesOtherChecks(n.Pos(), set.Get());
  } else {
    disproof_hand = NthChild(0).search_result.ProperHand();

    // 駒打ちならその駒を持っていないといけない
    if (is_drop(BestMove())) {
      auto pr = move_dropped_piece(BestMove());
      auto pr_cnt = hand_count(MergeHand(n.OrHand(), n.AndHand()), pr);
      auto disproof_pr_cnt = hand_count(disproof_hand, pr);
      if (pr_cnt - disproof_pr_cnt <= 0) {
        // もし現局面の攻め方の持ち駒が disproof_hand だった場合、打とうとしている駒 pr が攻め方に独占されているため
        // 受け方は BestMove() を着手することができない。そのため、攻め方の持ち駒を何枚か受け方に渡す必要がある。
        sub_hand(disproof_hand, pr, disproof_pr_cnt);
        add_hand(disproof_hand, pr, pr_cnt - 1);
      }
    }
  }

  return {NodeState::kDisprovenState, kInfinitePnDn, 0, disproof_hand};
}

SearchResult ChildrenCache::GetUnknownResult(const Node& n) const {
  if (or_node_) {
    return {NodeState::kOtherState, NthChild(0).Pn(), delta_, n.OrHand()};
  } else {
    return {NodeState::kOtherState, delta_, NthChild(0).Dn(), n.OrHand()};
  }
}

PnDn ChildrenCache::SecondPhi() const {
  if (children_len_ <= 1) {
    return kInfinitePnDn;
  }
  auto& second_best_child = NthChild(1);
  return second_best_child.Phi(or_node_);
}

PnDn ChildrenCache::DeltaExceptBestMove() const {
  auto& best_child = NthChild(0);
  return delta_ - best_child.Delta(or_node_);
}

bool ChildrenCache::Compare(const NodeCache& lhs, const NodeCache& rhs) const {
  if (or_node_) {
    if (lhs.Pn() != rhs.Pn()) {
      return lhs.Pn() < rhs.Pn();
    }
  } else {
    if (lhs.Dn() != rhs.Dn()) {
      return lhs.Dn() < rhs.Dn();
    }
  }

  auto lstate = lhs.search_result.GetNodeState();
  auto rstate = rhs.search_result.GetNodeState();
  if (lstate != rstate) {
    if (or_node_) {
      return static_cast<std::uint32_t>(lstate) < static_cast<std::uint32_t>(rstate);
    } else {
      return static_cast<std::uint32_t>(lstate) > static_cast<std::uint32_t>(rstate);
    }
  }

  return lhs.move.value < rhs.move.value;
}

template ChildrenCache::ChildrenCache(TranspositionTable& tt, const Node& n, bool first_search, NodeTag<false>);
template ChildrenCache::ChildrenCache(TranspositionTable& tt, const Node& n, bool first_search, NodeTag<true>);
}  // namespace komori