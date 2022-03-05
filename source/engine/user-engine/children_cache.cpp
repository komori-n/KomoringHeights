#include "children_cache.hpp"

#include <algorithm>
#include <numeric>

#include "../../mate/mate.h"
#include "initial_estimation.hpp"
#include "move_picker.hpp"
#include "node.hpp"

namespace komori {
namespace {
constexpr PnDn kSumSwitchThreshold = kInfinitePnDn / 16;
/// max で Delta を計算するときの調整項。詳しくは ChildrenCache::GetDelta() のコメントを参照。
constexpr PnDn kMaxDeltaBias = 2;

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

detail::Child MakeRepetitionChild(ExtMove move, Hand hand) {
  detail::Child child;

  child.move = move;
  child.search_result = SearchResult{RepetitionData{}};
  child.is_first = false;

  return child;
}

detail::Child MakeNonRepetitionChild(TranspositionTable& tt,
                                     Node& n,
                                     ExtMove move,
                                     bool& is_sum_delta,
                                     bool& does_have_old_child) {
  detail::Child child;
  child.move = move;
  child.query = tt.GetChildQuery(n, move.move);
  auto* entry = child.query.LookUpWithoutCreation();

  child.is_first = entry->IsFirstVisit();
  if (auto unknown = entry->TryGetUnknown()) {
    if (unknown->MinDepth() < n.GetDepth()) {
      does_have_old_child = true;
    }

    if (child.is_first) {
      auto [pn, dn] = InitialPnDn(n, move.move);
      auto new_pn = std::max(pn, unknown->Pn());
      auto new_dn = std::max(dn, unknown->Dn());
      if (new_pn != unknown->Pn() || new_dn != unknown->Dn()) {
        unknown->UpdatePnDn(new_pn, new_dn);
      }
    }
  }

  if (entry->IsNotFinal() && child.Delta(n.IsOrNode()) > kSumSwitchThreshold) {
    // Delta の値が大きすぎるとオーバーフローしてしまう恐れがあるので、max で計算する
    is_sum_delta = false;
  }

  auto hand_after = n.OrHandAfter(move.move);
  child.search_result = entry->Simplify(hand_after);

  return child;
}
}  // namespace

ChildrenCache::ChildrenCache(TranspositionTable& tt,
                             Node& n,
                             bool first_search,
                             BitSet64 sum_mask,
                             ChildrenCache* parent)
    : or_node_{n.IsOrNode()}, sum_mask_{sum_mask}, parent_{parent} {
  // 1 手詰めの場合、指し手生成をサボることができる
  // が、AndNode の 2 手詰めルーチンで mate_1ply を呼ぶのでここでやっても意味がない

  // 後で子局面を良さげ順に並び替えるため、ordering=true で指し手生成を行う
  for (auto&& move : MovePicker{n, true}) {
    auto& curr_idx = idx_[children_len_] = children_len_;
    auto& child = children_[children_len_++];
    if (n.IsRepetitionOrInferiorAfter(move.move)) {
      // どう見ても千日手の局面 or どう見ても詰まない局面は読み進める必要がない
      // 置換表 LookUp の回数を減らすために別処理に分ける
      child = MakeRepetitionChild(move, n.OrHand());
    } else {
      bool is_sum_node = IsSumDeltaNode(n, move);
      child = MakeNonRepetitionChild(tt, n, move, is_sum_node, does_have_old_child_);

      if (is_sum_node) {
        sum_mask_.Set(curr_idx);
      }

      if (!sum_mask_.Test(curr_idx)) {
        max_node_num_++;
      }

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
          DisprovenData disproven_data = {hand2, MOVE_NONE, MakeMateLen(0, hand2)};
          SearchResult dummy_result = {std::move(disproven_data), kMinimumSearchedAmount};
          UpdateNthChildWithoutSort(curr_idx, dummy_result);
        } else if (auto [best_move, proof_hand] = CheckMate1Ply(n); proof_hand != kNullHand) {
          // best_move を選ぶと1手詰み。

          // 置換表に詰みであることを保存したいので、child.search_result の直接更新ではなく Update 関数をちゃんと呼ぶ
          ProvenData proven_data = {proof_hand, best_move, MakeMateLen(1, proof_hand)};
          SearchResult dummy_result = {std::move(proven_data), kMinimumSearchedAmount};
          UpdateNthChildWithoutSort(curr_idx, dummy_result);
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

BitSet64 ChildrenCache::BestMoveSumMask() const {
  auto& best_child = NthChild(0);
  if (auto unknown = best_child.search_result.TryGetUnknown()) {
    return BitSet64{unknown->Secret()};
  }

  return BitSet64{};
}

void ChildrenCache::UpdateBestChild(const SearchResult& search_result) {
  UpdateNthChildWithoutSort(0, search_result);

  auto& old_best_child = NthChild(0);
  // UpdateNthChildWithoutSort() の内部で sum_mask_ の値が変わる可能性があるが、
  // sum_delta_except_best_, max_delta_except_best_ の値には関係ないので更新後の値のみ使う
  bool old_is_sum_delta = IsSumChild(0);
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

  if (IsSumChild(0)) {
    sum_delta_except_best_ -= new_best_child.Delta(or_node_);
  } else if (new_best_child.Delta(or_node_) < max_delta_except_best_) {
    // new_best_child を抜いても max_delta_except_best_ の値は変わらない
  } else {
    // max_delta_ の再計算が必要
    RecalcDelta();
  }
}

SearchResult ChildrenCache::CurrentResult(const Node& n) const {
  PnDn pn = or_node_ ? GetPhi() : GetDelta();
  PnDn dn = or_node_ ? GetDelta() : GetPhi();
  if (pn == 0) {
    return GetProvenResult(n);
  } else if (dn == 0) {
    return GetDisprovenResult(n);
  } else {
    return GetUnknownResult(n);
  }
}

std::pair<PnDn, PnDn> ChildrenCache::ChildThreshold(PnDn thpn, PnDn thdn) const {
  // pn/dn で考えるよりも phi/delta で考えたほうがわかりやすい
  // そのため、いったん phi/delta の世界に変換して、最後にもとに戻す

  auto thphi = Phi(thpn, thdn, or_node_);
  auto thdelta = Delta(thpn, thdn, or_node_);
  auto child_thphi = std::min(thphi, GetSecondPhi() + 1);
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

  if (!child.search_result.IsFinal() && child.Delta(or_node_) > kSumSwitchThreshold) {
    sum_mask_.Reset(idx_[i]);
  }
}

SearchResult ChildrenCache::GetProvenResult(const Node& n) const {
  Hand proof_hand = kNullHand;
  SearchedAmount amount = 0;
  Move best_move = MOVE_NONE;
  MateLen mate_len = kZeroMateLen;

  if (or_node_) {
    auto& best_child = NthChild(0);
    proof_hand = BeforeHand(n.Pos(), best_child.move, best_child.search_result.FrontHand());
    best_move = best_child.move;
    mate_len = best_child.search_result.FrontMateLen() + 1;
    amount = best_child.search_result.GetSearchedAmount();
  } else {
    // 子局面の証明駒の極小集合を計算する
    HandSet set{ProofHandTag{}};
    for (std::size_t i = 0; i < children_len_; ++i) {
      const auto& child = NthChild(i);
      set.Update(child.search_result.FrontHand());
      amount = std::max(amount, child.search_result.GetSearchedAmount());
      if (child.search_result.FrontMateLen() > mate_len) {
        best_move = child.move;
        mate_len = child.search_result.FrontMateLen() + 1;
      }
    }
    proof_hand = set.Get(n.Pos());

    // amount の総和を取ると値が大きくなりすぎるので、
    //   amount = max(child_amount) + children_len_ - 1
    // により計算する
    amount += children_len_ - 1;
  }

  ProvenData proven_data = {proof_hand, best_move, mate_len};
  return {std::move(proven_data), amount};
}

SearchResult ChildrenCache::GetDisprovenResult(const Node& n) const {
  // children_ は千日手エントリが手前に来るようにソートされているので、以下のようにして千日手判定ができる
  if (children_len_ > 0 && NthChild(0).search_result.GetNodeState() == NodeState::kRepetitionState) {
    return SearchResult{RepetitionData{}};
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
      set.Update(BeforeHand(n.Pos(), child.move, child.search_result.FrontHand()));
      amount = std::max(amount, child.search_result.GetSearchedAmount());
      if (child.search_result.FrontMateLen() > mate_len) {
        best_move = child.move;
        mate_len = child.search_result.FrontMateLen() + 1;
      }
    }
    amount += children_len_ - 1;
    disproof_hand = set.Get(n.Pos());
  } else {
    auto& best_child = NthChild(0);
    disproof_hand = best_child.search_result.FrontHand();
    best_move = best_child.move;
    mate_len = best_child.search_result.FrontMateLen() + 1;
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

  DisprovenData disproven_data = {disproof_hand, best_move, mate_len};
  return {std::move(disproven_data), amount};
}

SearchResult ChildrenCache::GetUnknownResult(const Node& n) const {
  auto& child = NthChild(0);
  SearchedAmount amount = child.search_result.GetSearchedAmount() + children_len_ - 1;
  if (or_node_) {
    UnknownData unknown_data = {child.Pn(), GetDelta(), n.OrHand(), n.GetDepth(), sum_mask_.Value()};
    return {std::move(unknown_data), amount};
  } else {
    UnknownData unknown_data = {GetDelta(), child.Dn(), n.OrHand(), n.GetDepth(), sum_mask_.Value()};
    return {std::move(unknown_data), amount};
  }
}

PnDn ChildrenCache::NewThdeltaForBestMove(PnDn thdelta) const {
  auto& best_child = NthChild(0);
  PnDn delta_except_best = sum_delta_except_best_;
  if (IsSumChild(0)) {
    delta_except_best += max_delta_except_best_;
  }

  // 計算の際はオーバーフローに注意
  if (thdelta >= sum_delta_except_best_) {
    return Clamp(thdelta - delta_except_best);
  }

  return 0;
}

void ChildrenCache::RecalcDelta() {
  sum_delta_except_best_ = 0;
  max_delta_except_best_ = 0;

  for (std::size_t i = 1; i < children_len_; ++i) {
    const auto& child = NthChild(i);
    if (IsSumChild(i)) {
      sum_delta_except_best_ += child.Delta(or_node_);
    } else {
      max_delta_except_best_ = std::max(max_delta_except_best_, child.Delta(or_node_));
    }
  }
}

PnDn ChildrenCache::GetPhi() const {
  if (children_len_ == 0) {
    return kInfinitePnDn;
  }

  return NthChild(0).Phi(or_node_);
}

PnDn ChildrenCache::GetDelta() const {
  if (children_len_ == 0) {
    return 0;
  }

  const auto& best_child = NthChild(0);
  // 差分計算用の値を予め持っているので、高速に計算できる
  auto sum_delta = sum_delta_except_best_;
  auto max_delta = max_delta_except_best_;
  if (IsSumChild(0)) {
    sum_delta += best_child.Delta(or_node_);
  } else {
    max_delta = std::max(max_delta, best_child.Delta(or_node_));
  }

  // 定義通りに
  //    delta = Σ(...) + max(...)
  // で計算すると、合駒や遠隔王手が続く局面のときに max(...) の値が実態以上に小さくなることがある。
  //
  // 例）sfen l3r2kl/6g2/2+Ppppnpp/2p4N1/3Pn1p1P/2P6/L1Ss1PP2/9/1K1S4L w 2BGNPr2gs4p 1
  //     S*9h から 9 筋で精算するとたくさん遠隔王手がかけられる。玉方の応手はすべて合駒のため pn は小さい値（6ぐらい）
  //     が続くが、詰みを示すためには膨大な局面を展開する必要がある。
  //
  // そのため、
  //    max(...)
  // の項が存在するときは delta の値を少し減点（数字を大きく）する。このような処理を追加することで、親局面から見た
  // 現局面の phi 値が大きくなり、この手順が少しだけ選ばれづらくなる。
  if (max_delta > 0) {
    max_delta += max_node_num_ / kMaxDeltaBias;
  }

  return sum_delta + max_delta;
}

PnDn ChildrenCache::GetSecondPhi() const {
  if (children_len_ <= 1) {
    return kInfinitePnDn;
  }
  auto& second_best_child = NthChild(1);
  return second_best_child.Phi(or_node_);
}

bool ChildrenCache::Compare(const detail::Child& lhs, const detail::Child& rhs) const {
  // or_node_ と move.value を参照しなければならないので ChildrenCache の内部で定義している

  if (lhs.Phi(or_node_) != rhs.Phi(or_node_)) {
    return lhs.Phi(or_node_) < rhs.Phi(or_node_);
  } else if (lhs.Delta(or_node_) != rhs.Delta(or_node_)) {
    return lhs.Delta(or_node_) > rhs.Delta(or_node_);
  }

  if (lhs.Dn() == 0 && rhs.Dn() == 0) {
    // DisprovenState と RepetitionState はちゃんと順番をつけなければならない
    // - repetition -> まだ頑張れば詰むかもしれない
    // - disproven -> どうやっても詰まない
    auto lstate = lhs.search_result.GetNodeState();
    auto rstate = rhs.search_result.GetNodeState();

    // or node -> repetition < disproven になってほしい（repetition なら別経路だと詰むかもしれない）
    // and node -> disproven < repetition になってほしい（disproven なら経路に関係なく詰まないから）
    // -> !or_node ^ (int)repetition < (int)disproven
    if (lstate != rstate) {
      return !or_node_ ^ (static_cast<int>(lstate) < static_cast<int>(rstate));
    }
  }

  return lhs.move.value < rhs.move.value;
}
}  // namespace komori
