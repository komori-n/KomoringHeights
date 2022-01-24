#include "children_cache.hpp"

#include <algorithm>
#include <numeric>

#include "../../mate/mate.h"
#include "initial_estimation.hpp"
#include "move_picker.hpp"
#include "node.hpp"

namespace komori {
namespace {
/// 詰み手数と持ち駒から MateLen を作る
inline MateLen MakeMateLen(Depth depth, Hand hand) {
  return {static_cast<std::uint16_t>(depth), static_cast<std::uint16_t>(CountHand(hand))};
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

ChildrenCache::Child ChildrenCache::Child::FromRepetitionMove(ExtMove move, Hand hand) {
  Child cache;

  cache.move = move;
  cache.search_result = {NodeState::kRepetitionState, kMinimumSearchedAmount, kInfinitePnDn, 0, hand};
  cache.is_first = false;
  cache.is_sum_delta = true;

  // 他の変数は時間節約のために未初期化のままにする

  return cache;
}

ChildrenCache::Child ChildrenCache::Child::FromNonRepetitionMove(TranspositionTable& tt,
                                                                 Node& n,
                                                                 ExtMove move,
                                                                 bool is_sum_delta,
                                                                 bool& does_have_old_child) {
  Child cache;
  cache.move = move;
  cache.is_sum_delta = is_sum_delta;
  cache.query = tt.GetChildQuery(n, move.move);
  auto* entry = cache.query.LookUpWithoutCreation();

  if (auto unknown = entry->TryGetUnknown()) {
    if (unknown->MinDepth() < n.GetDepth()) {
      does_have_old_child = true;
    }
  }

  cache.is_first = entry->IsFirstVisit();
  if (cache.is_first) {
    auto [pn, dn] = InitialPnDn(n, move.move);
    pn = std::max(pn, entry->Pn());
    dn = std::max(dn, entry->Dn());
    entry->UpdatePnDn(pn, dn, 0);
  }

  auto hand_after = n.OrHandAfter(move.move);
  cache.search_result = {*entry, hand_after};

  return cache;
}

ChildrenCache::ChildrenCache(TranspositionTable& tt, Node& n, bool first_search) : or_node_{n.IsOrNode()} {
  // 1 手詰めの場合、指し手生成をサボることができる
  // が、AndNode の 2 手詰めルーチンで mate_1ply を呼ぶのでここでやっても意味がない

  // 後で子局面を良さげ順に並び替えるため、ordering=true で指し手生成を行う
  for (auto&& move : MovePicker{n, true}) {
    auto& curr_idx = idx_[children_len_] = children_len_;
    auto& child = children_[children_len_++];
    if (n.IsRepetitionOrInferiorAfter(move.move)) {
      // どう見ても千日手の局面 or どう見ても詰まない局面は読み進める必要がない
      // 置換表 LookUp の回数を減らすために別処理に分ける
      child = Child::FromRepetitionMove(move, n.OrHand());
    } else {
      child = Child::FromNonRepetitionMove(tt, n, move, IsSumDeltaNode(n, move), does_have_old_child_);

      // AND node の first search の場合、1手掘り進めてみる（2手詰ルーチン）
      // OR node の場合、詰みかどうかを高速に判定できないので first_search でも先読みはしない
      if (!or_node_ && first_search && child.is_first) {
        n.DoMove(move.move);
        // 1手詰めチェック & 0手不詰チェック
        // 0手不詰チェックを先にした方が 1% ぐらい高速

        if (!DoesHaveMatePossibility(n.Pos())) {
          // 明らかに王手がかけられないので不詰

          auto hand2 = HandSet{DisproofHandTag{}}.Get(n.Pos());
          // 置換表に不詰であることを保存したいので、child.search_result の直接更新ではなく Update 関数をちゃんと呼ぶ
          SearchResult dummy_entry = {
              NodeState::kDisprovenState, kMinimumSearchedAmount, kInfinitePnDn, 0, hand2, MOVE_NONE,
              MakeMateLen(0, hand2)};
          UpdateNthChildWithoutSort(curr_idx, dummy_entry);
        } else if (auto [best_move, proof_hand] = CheckMate1Ply(n); proof_hand != kNullHand) {
          // best_move を選ぶと1手詰み。

          // 置換表に詰みであることを保存したいので、child.search_result の直接更新ではなく Update 関数をちゃんと呼ぶ
          auto after_hand = AfterHand(n.Pos(), best_move, proof_hand);
          SearchResult dummy_entry = {
              NodeState::kProvenState,   kMinimumSearchedAmount, 0, kInfinitePnDn, proof_hand, best_move,
              MakeMateLen(1, after_hand)};
          UpdateNthChildWithoutSort(curr_idx, dummy_entry);
        }
        n.UndoMove(move.move);
      }
    }

    // 1つでも（現在の手番側から見て）勝ちの手があるならそれを選べばOK。これ以上探索しても
    // CurrentResult() の結果はほとんど変化しない。

    if (child.Phi(or_node_) == 0) {
      break;
    }

    // ここで Delta() >= thdelta を根拠に子局面の展開をやめると、CurrentResult() の結果があまりよくなくなるのでダメ。
  }

  std::sort(idx_.begin(), idx_.begin() + children_len_,
            [this](const auto& lhs, const auto& rhs) { return Compare(children_[lhs], children_[rhs]); });

  RecalcDelta();
}

void ChildrenCache::UpdateBestChild(const SearchResult& search_result) {
  UpdateNthChildWithoutSort(0, search_result);

  auto& old_best_child = NthChild(0);
  bool old_is_sum_delta = old_best_child.is_sum_delta;
  PnDn old_delta = old_best_child.Delta(or_node_);

  // idx=0 の更新を受けて子ノードをソートし直す
  // [1, n) はソート済なので、insertion sort で高速にソートできる
  std::size_t j = 0 + 1;
  while (j < children_len_ && Compare(NthChild(j), NthChild(0))) {
    ++j;
  }
  std::rotate(idx_.begin(), idx_.begin() + 1, idx_.begin() + j);

  // RecalcDelta() をすると時間がかかるので、差分更新で済む場合はそうする
  auto& new_best_child = NthChild(0);
  if (old_is_sum_delta) {
    sum_delta_except_best_ += old_delta;
  } else {
    max_delta_except_best_ = std::max(max_delta_except_best_, old_delta);
  }

  if (new_best_child.is_sum_delta) {
    sum_delta_except_best_ -= new_best_child.Delta(or_node_);
  } else if (new_best_child.Delta(or_node_) < max_delta_except_best_) {
    // new_best_child を抜いても max_delta_except_best_ の値は変わらない
  } else {
    // max_delta_ の再計算が必要
    RecalcDelta();
  }
}

SearchResult ChildrenCache::CurrentResult(const Node& n) const {
  if (children_len_ > 0 && NthChild(0).Phi(or_node_) == 0) {
    // 手番側の勝ち
    if (or_node_) {
      return GetProvenResult(n);
    } else {
      return GetDisprovenResult(n);
    }
  } else if (GetDelta() == 0) {
    // 手番側の負け
    if (or_node_) {
      return GetDisprovenResult(n);
    } else {
      return GetProvenResult(n);
    }
  } else {
    return GetUnknownResult(n);
  }
}

std::pair<PnDn, PnDn> ChildrenCache::ChildThreshold(PnDn thpn, PnDn thdn) const {
  // pn/dn で考えるよりも phi/delta で考えたほうがわかりやすい
  // そのため、いったん phi/delta の世界に変換して、最後にもとに戻す

  auto thphi = Phi(thpn, thdn, or_node_);
  auto thdelta = Delta(thpn, thdn, or_node_);
  auto child_thphi = std::min(thphi, SecondPhi() + 1);
  auto child_thdelta = NewThdeltaForBestMove(thdelta);

  if (or_node_) {
    return {child_thphi, child_thdelta};
  } else {
    return {child_thdelta, child_thphi};
  }
}

void ChildrenCache::UpdateNthChildWithoutSort(std::size_t i, const SearchResult& search_result) {
  auto& child = NthChild(i);
  // Update したということはもう初探索ではないはず
  child.is_first = false;
  child.search_result = search_result;

  // このタイミングで置換表に登録する
  // なお、デストラクトまで置換表登録を遅延させると普通に性能が悪くなる（一敗）
  child.query.SetResult(child.search_result);
}

SearchResult ChildrenCache::GetProvenResult(const Node& n) const {
  Hand proof_hand = kNullHand;
  SearchedAmount amount = 0;
  Move best_move = MOVE_NONE;
  MateLen mate_len = kZeroMateLen;

  if (or_node_) {
    auto& best_child = NthChild(0);
    proof_hand = BeforeHand(n.Pos(), best_child.move, best_child.search_result.ProperHand());
    best_move = best_child.move;
    mate_len = best_child.search_result.GetMateLen() + 1;
    amount = best_child.search_result.GetSearchedAmount();
  } else {
    // 子局面の証明駒の極小集合を計算する
    HandSet set{ProofHandTag{}};
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = NthChild(i);
      set.Update(child.search_result.ProperHand());
      amount = std::max(amount, child.search_result.GetSearchedAmount());
      if (child.search_result.GetMateLen() > mate_len) {
        best_move = child.move;
        mate_len = child.search_result.GetMateLen() + 1;
      }
    }
    proof_hand = set.Get(n.Pos());

    // amount の総和を取ると値が大きくなりすぎるので、
    //   amount = max(child_amount) + children_len_ - 1
    // により計算する
    amount += children_len_ - 1;
  }

  return {NodeState::kProvenState, amount, 0, kInfinitePnDn, proof_hand, best_move, mate_len};
}

SearchResult ChildrenCache::GetDisprovenResult(const Node& n) const {
  // children_ は千日手エントリが手前に来るようにソートされているので、以下のようにして千日手判定ができる
  if (children_len_ > 0 && NthChild(0).search_result.GetNodeState() == NodeState::kRepetitionState) {
    return {NodeState::kRepetitionState, NthChild(0).search_result.GetSearchedAmount(), kInfinitePnDn, 0, n.OrHand()};
  }

  // フツーの不詰
  Hand disproof_hand = kNullHand;
  Move best_move = MOVE_NONE;
  MateLen mate_len = kZeroMateLen;
  SearchedAmount amount = 0;
  if (or_node_) {
    // 子局面の反証駒の極大集合を計算する
    HandSet set{DisproofHandTag{}};
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = NthChild(i);
      set.Update(BeforeHand(n.Pos(), child.move, child.search_result.ProperHand()));
      amount = std::max(amount, child.search_result.GetSearchedAmount());
      if (child.search_result.GetMateLen() > mate_len) {
        best_move = child.move;
        mate_len = child.search_result.GetMateLen() + 1;
      }
    }
    amount += children_len_ - 1;
    disproof_hand = set.Get(n.Pos());
  } else {
    auto& best_child = NthChild(0);
    disproof_hand = best_child.search_result.ProperHand();
    best_move = best_child.move;
    mate_len = best_child.search_result.GetMateLen() + 1;
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

  return {NodeState::kDisprovenState, amount, kInfinitePnDn, 0, disproof_hand, best_move, mate_len};
}

SearchResult ChildrenCache::GetUnknownResult(const Node& n) const {
  auto& child = NthChild(0);
  SearchedAmount amount = child.search_result.GetSearchedAmount() + children_len_ - 1;
  if (or_node_) {
    return {NodeState::kOtherState, amount, child.Pn(), GetDelta(), n.OrHand()};
  } else {
    return {NodeState::kOtherState, amount, GetDelta(), child.Dn(), n.OrHand()};
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
  auto& best_child = NthChild(0);
  // best_dn := best_child.Dn() のとき、dn が sum_delta かどうかで Delta 値の計算方法が変わる
  //    dn = best_dn + sum_delta_except_best_ + max_delta_except_best_
  //    dn = sum_delta_except_best_ + std::max(max_delta_except_best_, best_dn)

  // 計算の際はオーバーフローに注意
  if (best_child.is_sum_delta) {
    if (thdelta >= sum_delta_except_best_ + max_delta_except_best_) {
      return Clamp(thdelta - (sum_delta_except_best_ + max_delta_except_best_));
    }
  } else {
    if (thdelta >= sum_delta_except_best_) {
      return Clamp(thdelta - sum_delta_except_best_);
    }
  }

  return 0;
}

void ChildrenCache::RecalcDelta() {
  sum_delta_except_best_ = 0;
  max_delta_except_best_ = 0;

  for (std::size_t i = 1; i < children_len_; ++i) {
    const auto& child = NthChild(i);
    if (child.is_sum_delta) {
      sum_delta_except_best_ += child.Delta(or_node_);
    } else {
      max_delta_except_best_ = std::max(max_delta_except_best_, child.Delta(or_node_));
    }
  }
}

PnDn ChildrenCache::GetDelta() const {
  if (children_len_ == 0) {
    return 0;
  }

  const auto& best_child = NthChild(0);
  // 差分計算用の値を予め持っているので、高速に計算できる
  if (best_child.is_sum_delta) {
    return sum_delta_except_best_ + max_delta_except_best_ + best_child.Delta(or_node_);
  } else {
    return sum_delta_except_best_ + std::max(max_delta_except_best_, best_child.Delta(or_node_));
  }
}

bool ChildrenCache::Compare(const Child& lhs, const Child& rhs) const {
  // or_node_ と move.value を参照しなければならないので ChildrenCache の内部で定義している

  if (lhs.Phi(or_node_) != rhs.Phi(or_node_)) {
    return lhs.Phi(or_node_) < rhs.Phi(or_node_);
  } else if (lhs.Delta(or_node_) != rhs.Delta(or_node_)) {
    return lhs.Delta(or_node_) < rhs.Delta(or_node_);
  }

  if (lhs.Dn() == 0 && rhs.Dn() == 0) {
    // DisprovenState と RepetitionState はちゃんと順番をつけなければならない
    // - repetition -> まだ頑張れば詰むかもしれない
    // - disproven -> どうやっても詰まない
    auto lstate = lhs.search_result.GetNodeState();
    auto rstate = rhs.search_result.GetNodeState();
    if (lstate != rstate) {
      if (or_node_) {
        if (lstate == NodeState::kRepetitionState) {
          return true;
        } else if (rstate == NodeState::kRepetitionState) {
          return false;
        }
      } else {
        if (lstate == NodeState::kRepetitionState) {
          return false;
        } else if (lstate == NodeState::kRepetitionState) {
          return true;
        }
      }
    }
  }

  return lhs.move.value < rhs.move.value;
}
}  // namespace komori
