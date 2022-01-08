#include "children_cache.hpp"

#include <numeric>

#include "../../mate/mate.h"
#include "initial_estimation.hpp"
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
 * @brief move はδ値をsumで計算すべきか／maxで計上すべきかを判定する
 *
 * 似たような子局面になる move が複数ある場合、δ値を定義通りに sum で計算すると局面を過小評価
 * （実際の値よりも大きく出る）ことがある。そのため、move の内容によっては sum ではなく max でδ値を計上したほうが良い。
 *
 * @return true   move に対するδ値は sum で計上すべき
 * @return false  move に対するδ値は max で計上すべき
 */
inline bool IsSumDeltaNode(const Node& n, Move move, bool or_node) {
  if (is_drop(move)) {
    // 駒打ち
    if (or_node) {
      if (move_dropped_piece(move) == BISHOP || move_dropped_piece(move) == ROOK) {
        // 飛車と角はだいたいどこから打っても同じ
        return false;
      }
    } else {
      // 合駒はだいたい何を打っても同じ
      return false;
    }
  } else {
    // 駒移動（駒打ちではない）
    Color us = n.Pos().side_to_move();
    Square from = from_sq(move);
    Square to = to_sq(move);
    Piece moved_piece = n.Pos().piece_on(from);
    PieceType moved_pr = type_of(moved_piece);
    if (EnemyField[us].test(from) || EnemyField[us].test(to)) {
      if (moved_pr == PAWN || moved_pr == BISHOP || moved_pr == ROOK) {
        // 歩、角、飛車は基本成ればいいので、成らなかった時のδ値を足す必要がない
        return false;
      }
    }
  }

  return true;
}

/**
 * @brief 1手詰めルーチン。詰む場合は証明駒を返す。
 *
 * @param n  現局面（OrNode）
 * @return Hand （詰む場合）証明駒／（詰まない場合）kNullHand
 */
inline std::pair<Move, Hand> CheckMate1Ply(Node& n) {
  if (!n.Pos().in_check()) {
    if (auto move = Mate::mate_1ply(n.Pos()); move != MOVE_NONE) {
      n.DoMove(move);
      auto hand = HandSet{ProofHandTag{}}.Get(n.Pos());
      n.UndoMove(move);

      return {move, BeforeHand(n.Pos(), move, hand)};
    }
  }
  return {MOVE_NONE, kNullHand};
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

  cache.search_result = {NodeState::kRepetitionState, kMinimumSearchedAmount, kInfinitePnDn, 0, hand};
  cache.is_first = false;
  cache.is_sum_delta = true;
  cache.depth = Depth{kMaxNumMateMoves};

  return cache;
}

template <bool kOrNode>
ChildrenCache::NodeCache ChildrenCache::NodeCache::FromUnknownMove(Node& n,
                                                                   LookUpQuery&& query,
                                                                   ExtMove move,
                                                                   Hand hand,
                                                                   bool is_sum_delta) {
  NodeCache cache;
  cache.move = move;
  cache.query = std::move(query);
  auto* entry = cache.query.LookUpWithoutCreation();
  cache.is_first = entry->IsFirstVisit();
  cache.is_sum_delta = is_sum_delta;
  if (auto unknown = entry->TryGetUnknown()) {
    cache.depth = unknown->MinDepth();
  } else {
    cache.depth = Depth{kMaxNumMateMoves};
  }

  if (cache.is_first) {
    auto [pn, dn] = InitialPnDn<kOrNode>(n, move.move);
    pn = std::max(pn, entry->Pn());
    dn = std::max(dn, entry->Dn());
    entry->UpdatePnDn(pn, dn, 0);
  }

  cache.search_result = {*entry, hand};

  return cache;
}

template <bool kOrNode>
ChildrenCache::ChildrenCache(TranspositionTable& tt, const Node& n, bool first_search, NodeTag<kOrNode>)
    : or_node_{kOrNode} {
  // DoMove() や UndoMove() をしたいので const を外す
  Node& nn = const_cast<Node&>(n);

  // 1 手詰めの場合、指し手生成をサボることができる
  // が、AndNode の 2 手詰めルーチンで mate_1ply を呼ぶのでここでやっても意味がない

  for (auto&& move : MovePicker{nn, true}) {
    auto& curr_idx = idx_[children_len_] = children_len_;
    auto& child = children_[children_len_++];
    if (nn.IsRepetitionAfter(move.move)) {
      child = NodeCache::FromRepetitionMove(move, nn.OrHand());
    } else {
      auto&& query = tt.GetChildQuery(nn, move.move);
      auto hand = kOrNode ? AfterHand(nn.Pos(), move.move, nn.OrHand()) : nn.OrHand();
      child = NodeCache::FromUnknownMove<kOrNode>(nn, std::move(query), move, hand, IsSumDeltaNode(nn, move, kOrNode));
      if (child.depth < nn.GetDepth()) {
        does_have_old_child_ = true;
      }

      // 受け方の first search の場合、1手掘り進めてみる
      if (!kOrNode && first_search && child.is_first) {
        nn.DoMove(move.move);
        // 1手詰めチェック
        if (auto [best_move, proof_hand] = CheckMate1Ply(nn); proof_hand != kNullHand) {
          // move を選ぶと1手詰み。
          SearchResult dummy_entry = {
              NodeState::kProvenState, kMinimumSearchedAmount, 0, kInfinitePnDn, proof_hand, best_move, 1};

          UpdateNthChildWithoutSort(curr_idx, dummy_entry, 0);
          nn.UndoMove(move.move);
          continue;
        }

        // 1手不詰チェック
        // 一見重そうな処理だが、実験したところここの if 文（これ以上王手ができるどうかの判定} を入れたほうが
        // 結果として探索が高速化される。
        if (!DoesHaveMatePossibility(n.Pos())) {
          auto hand2 = HandSet{DisproofHandTag{}}.Get(nn.Pos());
          child.search_result = {
              NodeState::kDisprovenState, kMinimumSearchedAmount, kInfinitePnDn, 0, hand2, MOVE_NONE, 0};
        }
        nn.UndoMove(move.move);
      }
    }

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
  if (CalcDelta() == 0) {
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
  auto child_thdelta = NewThdeltaForBestMove(thdelta);

  return or_node_ ? std::make_pair(child_thphi, child_thdelta) : std::make_pair(child_thdelta, child_thphi);
}

void ChildrenCache::UpdateNthChildWithoutSort(std::size_t i,
                                              const SearchResult& search_result,
                                              std::uint64_t move_count) {
  auto& child = NthChild(i);
  child.is_first = false;
  child.search_result = search_result;
  auto proper_hand = search_result.ProperHand();
  auto best_move = search_result.BestMove();
  auto len = search_result.GetSolutionLen();
  auto amount = Update(search_result.GetSearchedAmount(), move_count);

  switch (search_result.GetNodeState()) {
    case NodeState::kProvenState:
      child.query.SetProven(proper_hand, best_move, len, amount);
      break;
    case NodeState::kDisprovenState:
      child.query.SetDisproven(proper_hand, best_move, len, amount);
      break;
    case NodeState::kRepetitionState: {
      auto entry = child.query.LookUpWithCreation();
      child.query.SetRepetition(entry, amount);
    } break;
    default: {
      auto entry = child.query.LookUpWithCreation();
      entry->UpdatePnDn(search_result.Pn(), search_result.Dn(), amount);
    }
  }
}

SearchResult ChildrenCache::GetProvenResult(const Node& n) const {
  Hand proof_hand = kNullHand;
  SearchedAmount amount = 0;
  Move best_move = MOVE_NONE;
  Depth solution_len = 0;

  if (or_node_) {
    auto& best_child = NthChild(0);
    proof_hand = BeforeHand(n.Pos(), best_child.move, best_child.search_result.ProperHand());
    best_move = best_child.move;
    solution_len = best_child.search_result.GetSolutionLen() + 1;
    amount = best_child.search_result.GetSearchedAmount();
  } else {
    // 子局面の証明駒の極小集合を計算する
    HandSet set{ProofHandTag{}};
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = NthChild(i);
      set.Update(child.search_result.ProperHand());
      amount += child.search_result.GetSearchedAmount();
      if (child.search_result.GetSolutionLen() > solution_len) {
        best_move = child.move;
        solution_len = child.search_result.GetSolutionLen() + 1;
      }
    }
    proof_hand = set.Get(n.Pos());
  }

  return {NodeState::kProvenState, amount, 0, kInfinitePnDn, proof_hand, best_move, solution_len};
}

SearchResult ChildrenCache::GetDisprovenResult(const Node& n) const {
  // children_ は千日手エントリが手前に来るようにソートされているので、以下のようにして千日手判定ができる
  if (children_len_ > 0 && NthChild(0).search_result.GetNodeState() == NodeState::kRepetitionState) {
    return {NodeState::kRepetitionState, NthChild(0).search_result.GetSearchedAmount(), kInfinitePnDn, 0, n.OrHand()};
  }

  // フツーの不詰
  Hand disproof_hand = kNullHand;
  Move best_move = MOVE_NONE;
  Depth solution_len = 0;
  SearchedAmount amount = 0;
  if (or_node_) {
    // 子局面の反証駒の極大集合を計算する
    HandSet set{DisproofHandTag{}};
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = NthChild(i);
      set.Update(BeforeHand(n.Pos(), child.move, child.search_result.ProperHand()));
      amount += child.search_result.GetSearchedAmount();
      if (child.search_result.GetSolutionLen() > solution_len) {
        best_move = child.move;
        solution_len = child.search_result.GetSolutionLen() + 1;
      }
    }
    disproof_hand = set.Get(n.Pos());
  } else {
    auto& best_child = NthChild(0);
    disproof_hand = best_child.search_result.ProperHand();
    best_move = best_child.move;
    solution_len = best_child.search_result.GetSolutionLen() + 1;
    amount = best_child.search_result.GetSearchedAmount();

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

  return {NodeState::kDisprovenState, amount, kInfinitePnDn, 0, disproof_hand, best_move, solution_len};
}

SearchResult ChildrenCache::GetUnknownResult(const Node& n) const {
  auto& child = NthChild(0);
  auto amount = child.search_result.GetSearchedAmount();
  if (or_node_) {
    return {NodeState::kOtherState, amount, child.Pn(), CalcDelta(), n.OrHand()};
  } else {
    return {NodeState::kOtherState, amount, CalcDelta(), child.Dn(), n.OrHand()};
  }
}

PnDn ChildrenCache::SecondPhi() const {
  if (children_len_ <= 1) {
    return kInfinitePnDn;
  }
  auto& second_best_child = NthChild(1);
  return second_best_child.Phi(or_node_);
}

PnDn ChildrenCache::NewThdeltaForBestMove(PnDn thdelta) const {
  // まず best_child 抜きのδ値を計算しておく
  PnDn sum_delta{0};
  PnDn max_delta{0};
  for (std::size_t i = 1; i < children_len_; ++i) {
    const auto& child = NthChild(i);
    if (child.is_sum_delta) {
      sum_delta += child.Delta(or_node_);
    } else {
      max_delta = std::max(max_delta, child.Delta(or_node_));
    }
  }

  // 上記をもとに、best_child.Delta() がいくつ以上になったら thdelta を超えるか計算する
  auto& best_child = NthChild(0);
  if (best_child.is_sum_delta) {
    if (thdelta >= sum_delta + max_delta) {
      return Clamp(thdelta - (sum_delta + max_delta));
    }
  } else {
    if (thdelta >= sum_delta) {
      return Clamp(thdelta - sum_delta);
    }
  }
  // 現時点で thdelta >= Delta() (>= sum_delta + max_delta) なので、このパスは通らないはず
  return 0;
}

PnDn ChildrenCache::CalcDelta() const {
  PnDn sum_delta{0};
  PnDn max_delta{0};

  for (std::size_t i = 0; i < children_len_; ++i) {
    const auto& child = NthChild(i);
    if (child.is_sum_delta) {
      sum_delta += child.Delta(or_node_);
    } else {
      max_delta = std::max(max_delta, child.Delta(or_node_));
    }
  }

  return sum_delta + max_delta;
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