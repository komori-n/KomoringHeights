#include "children_cache.hpp"

#include <algorithm>
#include <numeric>

#include "../../mate/mate.h"
#include "initial_estimation.hpp"
#include "move_picker.hpp"
#include "node.hpp"

namespace komori {
namespace detail {
struct Edge {
  std::uint64_t board_key, child_board_key;
  Hand hand, child_hand;
  PnDn child_pn, child_dn;

  static std::optional<Edge> From(const SearchResult& entry, std::uint64_t child_board_key, Hand child_hand) {
    if (auto unknown = entry.TryGetUnknown()) {
      if (unknown->ParentBoardKey() != kNullKey) {
        auto board_key = unknown->ParentBoardKey();
        auto hand = unknown->ParentHand();
        auto child_pn = unknown->Pn();
        auto child_dn = unknown->Dn();

        return {Edge{board_key, child_board_key, hand, child_hand, child_pn, child_dn}};
      }
    }

    return std::nullopt;
  }

  static std::optional<Edge> From(TranspositionTable& tt, std::uint64_t child_board_key, Hand child_hand) {
    auto query = tt.GetQueryByKey(child_board_key, child_hand);
    auto* entry = query.LookUpWithoutCreation();

    return From(*entry, child_board_key, child_hand);
  }

  static std::optional<Edge> FromChild(TranspositionTable& tt, const Edge& child_edge) {
    return From(tt, child_edge.board_key, child_edge.hand);
  }
};

void Child::LookUp(Node& n) {
  auto* entry = query.LookUpWithoutCreation();

  if (auto unknown = entry->TryGetUnknown()) {
    if (entry->IsFirstVisit()) {
      auto [pn, dn] = InitialPnDn(n, move.move);
      auto new_pn = std::max(pn, unknown->Pn());
      auto new_dn = std::max(dn, unknown->Dn());
      if (new_pn != unknown->Pn() || new_dn != unknown->Dn()) {
        unknown->UpdatePnDn(new_pn, new_dn);
      }
    }
  }

  search_result = entry->Simplify(hand);
}
}  // namespace detail

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

detail::Child MakeRepetitionChild(ExtMove move) {
  detail::Child child;

  child.move = move;
  child.search_result = SearchResult{RepetitionData{}};
  child.board_key = kNullKey;
  child.hand = kNullHand;
  child.next_dep = 0;

  return child;
}

detail::Child MakeNonRepetitionChild(TranspositionTable& tt, Node& n, ExtMove move) {
  detail::Child child;
  auto hand_after = n.OrHandAfter(move.move);

  child.move = move;
  child.query = tt.GetChildQuery(n, move.move);
  child.board_key = n.Pos().board_key_after(move.move);
  child.hand = hand_after;
  child.search_result.Clear();
  child.next_dep = 0;

  return child;
}

/// edge で合流する分岐の合流元局面を求める
std::optional<std::pair<detail::Edge, bool>> FindKnownAncestor(TranspositionTable& tt,
                                                               const Node& n,
                                                               const detail::Edge& root_edge) {
  bool pn_flag = true;
  bool dn_flag = true;

  // [best move in TT]     node      [best move in search tree]
  //                        |
  //                       node★
  //                       /  \                                 |
  //                     |    node
  //                     |      |
  //                       ...
  //         root_edge-> |      |
  //                     |    node <-current position(n)
  //                      \   /
  //                      node <-child (NthChild(i))
  //
  // 上図のように、root_edge の親局面がすでに分岐元の可能性がある。
  if (n.ContainsInPath(root_edge.board_key, root_edge.hand)) {
    return {std::make_pair(root_edge, n.IsOrNode())};
  }

  bool or_node = !n.IsOrNode();
  auto last_edge = root_edge;
  for (Depth i = 0; i < n.GetDepth(); ++i) {
    auto next_edge = detail::Edge::FromChild(tt, last_edge);
    if (next_edge == std::nullopt) {
      break;
    }

    if (n.ContainsInPath(next_edge->board_key, next_edge->hand)) {
      if ((or_node && dn_flag) || (!or_node && pn_flag)) {
        return {std::make_pair(*next_edge, or_node)};
      } else {
        break;
      }
    }

    // 合流元局面が OR node だと仮定すると、dn の二重カウントを解消したいことになる。このとき、or_node かつ dn の値が
    // 大きく離れた edge が存在するなら、二重カウントによる影響はそれほど深刻ではない（二重カウントを解消する
    // 必要がない）と判断する
    //
    // 合流元局面が OR node/AND node のどちらであるかは合流局面を実際に見つけるまでは分からない。そのため、合流局面が
    // or_node であった時用のフラグを dn_flag、 and_node であった時用のフラグを pn_flag として両方を計算している。
    if (or_node) {
      if (next_edge->child_dn > last_edge.child_dn + 5) {
        dn_flag = false;
      }
    } else {
      if (next_edge->child_pn > last_edge.child_pn + 5) {
        pn_flag = false;
      }
    }

    // 合流元局面が or node/and node のいずれであっても二重カウントを解消する必要がない。よって、early exit できる。
    if (!pn_flag && !dn_flag) {
      break;
    }

    last_edge = *next_edge;
    or_node = !or_node;
  }

  return std::nullopt;
}

template <bool kOrNode>
bool Compare(const detail::Child& lhs, const detail::Child& rhs) {
  if (lhs.Phi(kOrNode) != rhs.Phi(kOrNode)) {
    return lhs.Phi(kOrNode) < rhs.Phi(kOrNode);
  } else if (lhs.Delta(kOrNode) != rhs.Delta(kOrNode)) {
    return lhs.Delta(kOrNode) > rhs.Delta(kOrNode);
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
      return !kOrNode ^ (static_cast<int>(lstate) < static_cast<int>(rstate));
    }
  }

  return lhs.move.value < rhs.move.value;
}
}  // namespace

ChildrenCache::ChildrenCache(TranspositionTable& tt,
                             Node& n,
                             bool first_search,
                             BitSet64 sum_mask,
                             ChildrenCache* parent)
    : or_node_{n.IsOrNode()},
      sum_mask_{sum_mask},
      curr_board_key_{n.Pos().state()->board_key()},
      or_hand_{n.OrHand()},
      parent_{parent} {
  // 後で子局面を良さげ順に並び替えるため、ordering=true で指し手生成を行う
  const auto mp = MovePicker{n, true};

  bool found_rep = false;

  const auto king_sq = n.Pos().king_square(n.Pos().side_to_move());
  std::array<std::size_t, 8> skip_tbl{};
  for (const auto& move : mp) {
    const auto i_raw = actual_len_++;
    auto& child = children_[i_raw];

    // どう見ても千日手 or 列島局面になる場合は読み進める必要がない
    // 置換表 LookUp の回数を減らすために別処理に分ける
    if (n.IsRepetitionOrInferiorAfter(move.move)) {
      // 千日手は複数あっても意味がないので、2 つ目以降は無視する
      if (!found_rep) {
        child = MakeRepetitionChild(move);
        const auto i = effective_len_++;
        idx_[i] = i_raw;

        if (!or_node_) {
          break;
        }
      }
      found_rep = true;
    } else {
      child = MakeNonRepetitionChild(tt, n, move);
      Expand(n, i_raw, first_search);

      if (!or_node_ && is_drop(move.move) && child.search_result.IsNotFinal()) {
        // 同じ位置への合駒は後回しにする
        const auto to = to_sq(move.move);
        const auto d = dist(king_sq, to);

        if (skip_tbl[d] > 0) {
          // child_dep が詰んだらこの手を読むようにする
          auto& child_dep = children_[skip_tbl[d] - 1];
          child_dep.next_dep = i_raw + 1;

          // to への合駒はすでに Expand しているので、この手は後回しにする
          // effective_len_ をいじれば展開した子をいないとみなすことができる
          effective_len_--;
        }

        skip_tbl[d] = i_raw + 1;
      }

      if (child.Phi(or_node_) == 0) {
        break;
      }
    }
  }

  if (or_node_) {
    std::sort(idx_.begin(), idx_.begin() + effective_len_,
              [this](const auto& lhs, const auto& rhs) { return Compare<true>(children_[lhs], children_[rhs]); });
  } else {
    std::sort(idx_.begin(), idx_.begin() + effective_len_,
              [this](const auto& lhs, const auto& rhs) { return Compare<false>(children_[lhs], children_[rhs]); });
  }
  RecalcDelta();

  if (effective_len_ > 0) {
    EliminateDoubleCount(tt, n, 0);
  }
}

BitSet64 ChildrenCache::BestMoveSumMask() const {
  auto& best_child = NthChild(0);
  if (auto unknown = best_child.search_result.TryGetUnknown()) {
    // secret には BitSet のビットを反転した値が格納されているので注意
    return BitSet64{~unknown->Secret()};
  }

  return BitSet64::Full();
}

void ChildrenCache::UpdateBestChild(const SearchResult& search_result) {
  auto new_child = UpdateNthChildWithoutSort(0, search_result);

  if (new_child) {
    // 余計なことを考えずに sort と delta 再計算を行う
    if (or_node_) {
      std::sort(idx_.begin(), idx_.begin() + effective_len_,
                [this](const auto& lhs, const auto& rhs) { return Compare<true>(children_[lhs], children_[rhs]); });
    } else {
      std::sort(idx_.begin(), idx_.begin() + effective_len_,
                [this](const auto& lhs, const auto& rhs) { return Compare<false>(children_[lhs], children_[rhs]); });
    }
    RecalcDelta();
  } else {
    auto& old_best_child = NthChild(0);
    // UpdateNthChildWithoutSort() の内部で sum_mask_ の値が変わる可能性があるが、
    // sum_delta_except_best_, max_delta_except_best_ の値には関係ないので更新後の値のみ使う
    bool old_is_sum_delta = IsSumChild(0);
    PnDn old_delta = old_best_child.Delta(or_node_);

    Refresh(0);
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
}

SearchResult ChildrenCache::CurrentResult(const Node& n) const {
  if (GetPn() == 0) {
    return GetProvenResult(n);
  } else if (GetDn() == 0) {
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

bool ChildrenCache::UpdateNthChildWithoutSort(std::size_t i, const SearchResult& search_result) {
  auto& child = NthChild(i);
  child.search_result = search_result;

  // このタイミングで置換表に登録する
  // なお、デストラクトまで置換表登録を遅延させると普通に性能が悪くなる（一敗）
  child.query.SetResult(child.search_result);

  if (!child.search_result.IsFinal() && child.Delta(or_node_) > kSumSwitchThreshold) {
    sum_mask_.Reset(idx_[i]);
  }

  if (search_result.Delta(or_node_) == 0 && child.next_dep > 0) {
    // 後回しにした手があるならそれを復活させる

    // 現在注目している子ノード
    auto i_raw = idx_[i];
    // i_raw の次に調べるべき子
    auto next_dep = children_[i_raw].next_dep;
    while (next_dep > 0) {
      children_[i_raw].next_dep = 0;
      i_raw = next_dep - 1;

      idx_[effective_len_++] = i_raw;
      if (children_[i_raw].Delta(or_node_) > 0) {
        // まだ結論の出ていない子がいた
        break;
      }

      // i_raw は結論が出ているので、次の後回しにした手 next_dep を調べる
      next_dep = children_[i_raw].next_dep;
    }

    return true;
  }

  return false;
}

void ChildrenCache::Expand(Node& n, const std::size_t i_raw, const bool first_search) {
  const auto i = effective_len_++;
  idx_[i] = i_raw;
  auto& child = children_[i_raw];
  const auto move = child.move;

  child.LookUp(n);

  bool is_sum_node = IsSumDeltaNode(n, move);
  if (auto unknown = child.search_result.TryGetUnknown()) {
    if (unknown->IsOldChild(n.GetDepth())) {
      does_have_old_child_ = true;
    }

    if (Delta(unknown->Pn(), unknown->Dn(), or_node_) > kSumSwitchThreshold) {
      // Delta の値が大きすぎるとオーバーフローしてしまう恐れがあるので、max で計算する
      is_sum_node = false;
    }
  }

  if (!is_sum_node) {
    sum_mask_.Reset(i_raw);
  }

  if (!sum_mask_.Test(i_raw)) {
    max_node_num_++;
  }

  // AND node の first search の場合、1手掘り進めてみる（2手詰ルーチン）
  // OR node の場合、詰みかどうかを高速に判定できないので first_search でも先読みはしない
  if (!or_node_ && first_search && child.IsFirstVisit()) {
    n.DoMove(move.move);
    // 1手詰めチェック & 0手不詰チェック
    // 0手不詰チェックを先にした方が 1% ぐらい高速

    if (!DoesHaveMatePossibility(n.Pos())) {
      // 明らかに王手がかけられないので不詰

      auto hand2 = HandSet{DisproofHandTag{}}.Get(n.Pos());
      // 置換表に不詰であることを保存したいので、child.search_result の直接更新ではなく Update 関数をちゃんと呼ぶ
      DisprovenData disproven_data = {hand2, MOVE_NONE, MakeMateLen(0, hand2)};
      SearchResult dummy_result = {std::move(disproven_data), kMinimumSearchedAmount};
      UpdateNthChildWithoutSort(i, dummy_result);
    } else if (auto [best_move, proof_hand] = CheckMate1Ply(n); proof_hand != kNullHand) {
      // best_move を選ぶと1手詰み。

      // 置換表に詰みであることを保存したいので、child.search_result の直接更新ではなく Update 関数をちゃんと呼ぶ
      ProvenData proven_data = {proof_hand, best_move, MakeMateLen(1, proof_hand)};
      SearchResult dummy_result = {std::move(proven_data), kMinimumSearchedAmount};
      UpdateNthChildWithoutSort(i, dummy_result);
    }
    n.UndoMove(move.move);
  }
}

void ChildrenCache::Refresh(const std::size_t i) {
  auto comparer = or_node_ ? Compare<true> : Compare<false>;

  // i 以外はソートされていることを利用して二分探索したい
  // i がソートされていないことに注意して、i の左側／右側のどちらに挿入すべきか場合分けして考える
  if (i == 0 || comparer(NthChild(i - 1), NthChild(i))) {
    auto itr = std::lower_bound(
        idx_.begin() + i + 1, idx_.begin() + effective_len_, idx_[i],
        [comparer, this](const auto& lhs, const auto& rhs) { return comparer(children_[lhs], children_[rhs]); });
    std::rotate(idx_.begin() + i, idx_.begin() + i + 1, itr);
  } else {
    auto itr = std::lower_bound(
        idx_.begin(), idx_.begin() + i, idx_[i],
        [comparer, this](const auto& lhs, const auto& rhs) { return comparer(children_[lhs], children_[rhs]); });
    std::rotate(itr, idx_.begin() + i, idx_.begin() + i + 1);
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
    for (std::size_t i = 0; i < effective_len_; ++i) {
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
    //   amount = max(child_amount) + actual_len_ - 1
    // により計算する
    amount += actual_len_ - 1;
  }

  ProvenData proven_data = {proof_hand, best_move, mate_len};
  return {std::move(proven_data), amount};
}

SearchResult ChildrenCache::GetDisprovenResult(const Node& n) const {
  // children_ は千日手エントリが手前に来るようにソートされているので、以下のようにして千日手判定ができる
  if (actual_len_ > 0 && NthChild(0).search_result.GetNodeState() == NodeState::kRepetitionState) {
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
    for (std::size_t i = 0; i < actual_len_; ++i) {
      const auto& child = NthChild(i);

      set.Update(BeforeHand(n.Pos(), child.move, child.search_result.FrontHand()));
      amount = std::max(amount, child.search_result.GetSearchedAmount());
      if (child.search_result.FrontMateLen() > mate_len) {
        best_move = child.move;
        mate_len = child.search_result.FrontMateLen() + 1;
      }
    }
    amount += actual_len_ - 1;
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
  SearchedAmount amount = child.search_result.GetSearchedAmount() + actual_len_ - 1;

  // secret には ~sum_mask_ を書いておく。ビット反転している理由は、secret のデフォルト値を 0 にしたいから
  UnknownData unknown_data = {GetPn(), GetDn(), or_hand_, n.GetDepth(), ~sum_mask_.Value()};
  if (parent_ != nullptr) {
    unknown_data.SetParent(parent_->curr_board_key_, parent_->or_hand_);
  }
  return {std::move(unknown_data), amount};
}

PnDn ChildrenCache::NewThdeltaForBestMove(PnDn thdelta) const {
  auto& best_child = NthChild(0);
  PnDn delta_except_best = sum_delta_except_best_;
  if (IsSumChild(0)) {
    delta_except_best += max_delta_except_best_;
  }

  // 計算の際はオーバーフローに注意
  if (thdelta >= delta_except_best) {
    return Clamp(thdelta - delta_except_best);
  }

  return 0;
}

void ChildrenCache::RecalcDelta() {
  sum_delta_except_best_ = 0;
  max_delta_except_best_ = 0;

  for (std::size_t i = 1; i < effective_len_; ++i) {
    const auto& child = NthChild(i);
    if (IsSumChild(i)) {
      sum_delta_except_best_ += child.Delta(or_node_);
    } else {
      max_delta_except_best_ = std::max(max_delta_except_best_, child.Delta(or_node_));
    }
  }
}

PnDn ChildrenCache::GetPn() const {
  if (or_node_) {
    return GetPhi();
  } else {
    return GetDelta();
  }
}

PnDn ChildrenCache::GetDn() const {
  if (or_node_) {
    return GetDelta();
  } else {
    return GetPhi();
  }
}

PnDn ChildrenCache::GetPhi() const {
  if (effective_len_ == 0) {
    return kInfinitePnDn;
  }

  return NthChild(0).Phi(or_node_);
}

PnDn ChildrenCache::GetDelta() const {
  auto [sum_delta, max_delta] = GetRawDelta();
  if (sum_delta == 0 && max_delta == 0) {
    return 0;
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

  // 後回しにしている子局面が存在する場合、その値をδ値に加算しないと局面を過大評価してしまう。
  //
  // 例） sfen +P5l2/4+S4/p1p+bpp1kp/6pgP/3n1n3/P2NP4/3P1NP2/2P2S3/3K3L1 b RGSL2Prb2gsl3p 159
  //      1筋の合駒を考える時、玉方が合駒を微妙に変えることで読みの深さを指数関数的に大きくできてしまう
  if (actual_len_ > effective_len_) {
    // 読みの後回しが原因の（半）無限ループを回避できればいいので、1点減点しておく
    sum_delta += 1;
  }

  return sum_delta + max_delta;
}

std::pair<PnDn, PnDn> ChildrenCache::GetRawDelta() const {
  if (effective_len_ == 0) {
    return {0, 0};
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

  return {sum_delta, max_delta};
}

PnDn ChildrenCache::GetSecondPhi() const {
  if (effective_len_ <= 1) {
    return kInfinitePnDn;
  }
  auto& second_best_child = NthChild(1);
  return second_best_child.Phi(or_node_);
}

void ChildrenCache::EliminateDoubleCount(TranspositionTable& tt, const Node& n, std::size_t i) {
  // [best move in TT]     node      [best move in search tree]
  //                        |
  //                       node★
  //          found_edge-> /  \                                 |
  //                   node   node
  //                     |      |
  //                       ...
  //                     |      |
  //                   node   node <-current position(n)
  //               edge-> \   /
  //                      node <-child (NthChild(i))
  //
  // 上記のような探索木の状態を考える。局面★で分岐した探索木の部分木が子孫ノードで合流している。このとき、局面★の
  // δ値の計算で合流ノード由来の値を二重で加算してしまう可能性がある。
  // このとき、pn/dn のどちらが二重カウントされるかは★のノード種別（OR node/AND node）にしか依存せず、合流局面の
  // ノード種別には依存しないことに注意。
  //
  // この関数は、上記のような局面の合流を検出し、二重カウント状態を解消する役割である。

  auto& child = NthChild(i);
  if (auto edge = detail::Edge::From(child.search_result, child.board_key, child.hand)) {
    if (edge->board_key != curr_board_key_) {
      // ここまでの条件分岐より、以下の局面グラフになっていることが分かった。
      //
      //                   node   node <-current position(n)
      //               edge-> \   /
      //                      node <-child (NthChild(i))
      //
      // 次に、親局面をたどって合流元局面を特定する。
      auto res = FindKnownAncestor(tt, n, *edge);
      if (res) {
        // found_edge で分岐して child で合流していることが分かったので、それを解消しにいく
        SetBranchRootMaxFlag(res->first, res->second);
      }
    }
  }
}

void ChildrenCache::SetBranchRootMaxFlag(const detail::Edge& edge, bool branch_root_is_or_node) {
  if (curr_board_key_ == edge.board_key && or_hand_ == edge.hand) {
    // 現局面が edge の分岐元。すなわち、edge と NthChild(0) が子孫局面が合流していることが分かった。
    // 何もケアしないと二重カウントが発生してしまうので、sum ではなく max でδ値を計算させるようにする。

    for (std::size_t i = 1; i < effective_len_; ++i) {
      auto& child = NthChild(i);
      if (child.board_key == edge.child_board_key && child.hand == edge.child_hand) {
        sum_mask_.Reset(idx_[0]);
        if (sum_mask_.Test(idx_[i])) {
          sum_mask_.Reset(idx_[i]);
          RecalcDelta();
        }

        break;
      }
    }
    return;
  }

  if (branch_root_is_or_node == or_node_) {
    const auto& best_child = NthChild(0);
    // max child でδ値が max_delta よりも小さい場合、NthChild(0) のδ値は上位ノードに伝播していない。
    // つまり、親局面でδ値の二重カウントは発生しないため、return できる。
    if (!IsSumChild(0) && best_child.Delta(or_node_) < max_delta_except_best_) {
      return;
    }

    if (sum_delta_except_best_ > 0) {
      return;
    }
  }

  // 局面を 1 手戻して edge の分岐局面がないかを再帰的に探す
  if (parent_ != nullptr) {
    parent_->SetBranchRootMaxFlag(edge, branch_root_is_or_node);
  }
}
}  // namespace komori
