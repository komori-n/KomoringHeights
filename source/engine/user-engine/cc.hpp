#ifndef KOMORI_NEW_CC_HPP_
#define KOMORI_NEW_CC_HPP_

#include <algorithm>
#include <optional>

#include "../../mate/mate.h"
#include "bitset.hpp"
#include "children_board_key.hpp"
#include "delayed_move_list.hpp"
#include "hands.hpp"
#include "initial_estimation.hpp"
#include "move_picker.hpp"
#include "node.hpp"
#include "tt.hpp"

namespace komori {
namespace detail {

class IndexTable {
 public:
  constexpr std::uint32_t Push(std::uint32_t i_raw) {
    const auto i = len_++;
    data_[i] = i_raw;
    return i;
  }
  constexpr void Pop() { --len_; }

  constexpr std::uint32_t operator[](std::uint32_t i) const { return data_[i]; }

  constexpr auto begin() { return data_.begin(); }
  constexpr auto begin() const { return data_.begin(); }
  constexpr auto end() { return data_.begin() + len_; }
  constexpr auto end() const { return data_.begin() + len_; }
  constexpr auto size() const { return len_; }
  constexpr bool empty() const { return len_ == 0; }
  constexpr std::uint32_t& front() { return data_[0]; }
  constexpr const std::uint32_t& front() const { return data_[0]; }
  constexpr std::uint32_t& back() { return data_[len_ - 1]; }
  constexpr const std::uint32_t& back() const { return data_[len_ - 1]; }

 private:
  std::array<std::uint32_t, kMaxCheckMovesPerNode> data_;
  std::uint32_t len_{0};
};

struct Edge {
  std::uint64_t board_key, child_board_key;
  Hand hand, child_hand;
  PnDn child_pn, child_dn;
};

inline bool DoesHaveMatePossibility(const Position& n) {
  auto us = n.side_to_move();
  auto them = ~us;
  auto hand = n.hand_of(us);
  auto king_sq = n.king_square(them);

  auto droppable_bb = ~n.pieces();
  for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
    if (hand_exists(hand, pr)) {
      if (pr == PAWN && (n.pieces(us, PAWN) & file_bb(file_of(king_sq)))) {
        continue;
      }

      if (droppable_bb.test(StepEffect(pr, them, king_sq))) {
        return true;
      }
    }
  }

  auto x = ((n.pieces(PAWN) & check_candidate_bb(us, PAWN, king_sq)) |
            (n.pieces(LANCE) & check_candidate_bb(us, LANCE, king_sq)) |
            (n.pieces(KNIGHT) & check_candidate_bb(us, KNIGHT, king_sq)) |
            (n.pieces(SILVER) & check_candidate_bb(us, SILVER, king_sq)) |
            (n.pieces(GOLDS) & check_candidate_bb(us, GOLD, king_sq)) |
            (n.pieces(BISHOP) & check_candidate_bb(us, BISHOP, king_sq)) | (n.pieces(ROOK_DRAGON)) |
            (n.pieces(HORSE) & check_candidate_bb(us, ROOK, king_sq))) &
           n.pieces(us);
  auto y = n.blockers_for_king(them) & n.pieces(us);

  return x | y;
}

inline std::pair<Move, Hand> CheckMate1Ply(Node& n) {
  if (!n.Pos().in_check()) {
    if (auto move = Mate::mate_1ply(n.Pos()); move != MOVE_NONE) {
      n.DoMove(move);
      auto hand = HandSet{ProofHandTag{}}.Get(n.Pos());
      n.UndoMove();

      return {move, BeforeHand(n.Pos(), move, hand)};
    }
  }
  return {MOVE_NONE, kNullHand};
}

/// edge で合流する分岐の合流元局面を求める
inline std::optional<std::pair<detail::Edge, bool>> FindKnownAncestor(tt::TranspositionTable& tt,
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
    auto query = tt.BuildQueryByKey(last_edge.board_key, last_edge.hand);
    const auto& result = query.LookUp(kMaxMateLen, false);
    if (result.IsFinal() || result.GetUnknownData().parent_board_key == kNullKey) {
      break;
    }
    const auto next_parent_board_key = result.GetUnknownData().parent_board_key;
    const auto next_parent_hand = result.GetUnknownData().parent_hand;
    const Edge next_edge{next_parent_board_key, last_edge.board_key, next_parent_hand,
                         last_edge.hand,        result.Pn(),         result.Dn()};

    if (n.ContainsInPath(next_edge.board_key, next_edge.hand)) {
      if ((or_node && dn_flag) || (!or_node && pn_flag)) {
        return {std::make_pair(next_edge, or_node)};
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
      if (next_edge.child_dn > last_edge.child_dn + 5) {
        dn_flag = false;
      }
    } else {
      if (next_edge.child_pn > last_edge.child_pn + 5) {
        pn_flag = false;
      }
    }

    // 合流元局面が or node/and node のいずれであっても二重カウントを解消する必要がない。よって、early exit できる。
    if (!pn_flag && !dn_flag) {
      break;
    }

    last_edge = next_edge;
    or_node = !or_node;
  }

  return std::nullopt;
}
}  // namespace detail

class ChildrenCache {
 private:
  auto MakeComparer() const {
    return [this](std::size_t i_raw, std::size_t j_raw) -> bool {
      const auto& left_result = results_[i_raw];
      const auto& right_result = results_[j_raw];

      if (left_result.Phi(or_node_) != right_result.Phi(or_node_)) {
        return left_result.Phi(or_node_) < right_result.Phi(or_node_);
      } else if (left_result.Delta(or_node_) != right_result.Delta(or_node_)) {
        return left_result.Delta(or_node_) > right_result.Delta(or_node_);
      }

      if (left_result.Dn() == 0 && right_result.Dn() == 0) {
        const auto l_is_rep = left_result.GetFinalData().is_repetition;
        const auto r_is_rep = right_result.GetFinalData().is_repetition;

        if (l_is_rep != r_is_rep) {
          return !or_node_ ^ (static_cast<int>(l_is_rep) < static_cast<int>(r_is_rep));
        }
      }

      return mp_[i_raw] < mp_[j_raw];
    };
  }

 public:
  ChildrenCache(tt::TranspositionTable& tt,
                const Node& n,
                MateLen len,
                bool first_search,
                BitSet64 sum_mask = BitSet64::Full(),
                ChildrenCache* parent = nullptr)
      : or_node_{n.IsOrNode()},
        mp_{n, true},
        delayed_move_list_{n, mp_},
        children_board_key_{n, mp_},
        len_{len},
        sum_mask_{sum_mask},
        parent_{parent},
        board_key_{n.BoardKey()},
        or_hand_{n.OrHand()} {
    bool found_rep = false;
    Node& nn = const_cast<Node&>(n);

    std::uint32_t next_i_raw = 0;
    for (const auto& move : mp_) {
      const auto i_raw = next_i_raw++;
      const auto hand_after = n.OrHandAfter(move.move);
      idx_.Push(i_raw);
      auto& result = results_[i_raw];
      auto& query = queries_[i_raw];

      if (!IsSumDeltaNode(n, move.move)) {
        sum_mask_.Reset(i_raw);
      }

      if (n.IsRepetitionOrInferiorAfter(move.move)) {
        if (!found_rep) {
          found_rep = true;
          result.InitFinal<false, true>(hand_after, len, 1);
        } else {
          idx_.Pop();
          continue;
        }
      } else {
        query = tt.BuildChildQuery(n, move.move);
        result =
            query.LookUp(does_have_old_child_, len - 1, false, [&n, &move]() { return InitialPnDn(n, move.move); });

        if (!result.IsFinal() && !or_node_ && first_search && result.GetUnknownData().is_first_visit) {
          nn.DoMove(move.move);
          if (!detail::DoesHaveMatePossibility(n.Pos())) {
            const auto hand = HandSet{DisproofHandTag{}}.Get(n.Pos());
            result.InitFinal<false>(hand, kMaxMateLen, 1);
            query.SetResult(result);
          } else if (auto [best_move, proof_hand] = detail::CheckMate1Ply(nn); proof_hand != kNullHand) {
            const auto proof_hand_after = AfterHand(n.Pos(), best_move, proof_hand);
            const auto len = MateLen::Make(1, static_cast<std::uint32_t>(CountHand(proof_hand_after)));

            if (len <= len_ - 1) {
              result.InitFinal<true>(proof_hand, len, 1);
            } else {
              result.InitFinal<false>(n.OrHand(), len.Prec(), 1);
            }

            query.SetResult(result);
          }
          nn.UndoMove();
        }

        if (!result.IsFinal() && delayed_move_list_.Prev(i_raw)) {
          idx_.Pop();
        }
      }

      if (result.Phi(or_node_) == 0) {
        break;
      }
    }

    std::sort(idx_.begin(), idx_.end(), MakeComparer());
    RecalcDelta();

    if (!idx_.empty()) {
      EliminateDoubleCount(tt, n);
    }
  }

  ChildrenCache(const ChildrenCache&) = delete;
  ChildrenCache(ChildrenCache&&) = delete;
  ChildrenCache& operator=(const ChildrenCache&) = delete;
  ChildrenCache& operator=(ChildrenCache&&) = delete;
  ~ChildrenCache() = default;

  Move BestMove() const { return mp_[idx_.front()].move; };
  bool DoesHaveOldChild() const { return does_have_old_child_; }
  bool FrontIsFirstVisit() const { return FrontResult().GetUnknownData().is_first_visit; }
  BitSet64 FrontSumMask() const {
    const auto& result = FrontResult();
    return BitSet64{~result.GetUnknownData().secret};
  }

  SearchResult CurrentResult(const Node& n) const {
    if (GetPn() == 0) {
      return GetProvenResult(n);
    } else if (GetDn() == 0) {
      return GetDisprovenResult(n);
    } else {
      return GetUnknownResult(n);
    }
  }

  void UpdateBestChild(const SearchResult& search_result) {
    const auto old_i_raw = idx_[0];
    auto& query = queries_[old_i_raw];
    auto& result = results_[old_i_raw];
    result = search_result;
    query.SetResult(search_result);

    if (search_result.Delta(or_node_) == 0 && delayed_move_list_.Next(old_i_raw)) {
      // 後回しにした手があるならそれを復活させる
      // curr_i_raw の次に調べるべき子
      auto curr_i_raw = delayed_move_list_.Next(old_i_raw);
      do {
        idx_.Push(*curr_i_raw);
        if (results_[*curr_i_raw].Delta(or_node_) > 0) {
          // まだ結論の出ていない子がいた
          break;
        }

        // curr_i_raw は結論が出ているので、次の後回しにした手 next_dep を調べる
        curr_i_raw = delayed_move_list_.Next(*curr_i_raw);
      } while (curr_i_raw.has_value());

      std::sort(idx_.begin(), idx_.end(), MakeComparer());
      RecalcDelta();
    } else {
      const bool old_is_sum_delta = sum_mask_[old_i_raw];
      if (old_is_sum_delta) {
        sum_delta_except_best_ += result.Delta(or_node_);
      } else {
        max_delta_except_best_ = std::max(max_delta_except_best_, result.Delta(or_node_));
      }

      ResortFront();

      const auto new_i_raw = idx_[0];
      const auto new_result = results_[new_i_raw];
      const bool new_is_sum_delta = sum_mask_[new_i_raw];
      if (new_is_sum_delta) {
        sum_delta_except_best_ -= new_result.Delta(or_node_);
      } else if (new_result.Delta(or_node_) < max_delta_except_best_) {
        // new_best_child を抜いても max_delta_except_best_ の値は変わらない
      } else {
        // max_delta_ の再計算が必要
        RecalcDelta();
      }
    }
  }

  std::pair<PnDn, PnDn> PnDnThresholds(PnDn thpn, PnDn thdn) const {
    // pn/dn で考えるよりも phi/delta で考えたほうがわかりやすい
    // そのため、いったん phi/delta の世界に変換して、最後にもとに戻す

    const auto thphi = Phi(thpn, thdn, or_node_);
    const auto thdelta = Delta(thpn, thdn, or_node_);
    const auto child_thphi = std::min(thphi, GetSecondPhi() + 1);
    const auto child_thdelta = NewThdeltaForBestMove(thdelta);

    if (or_node_) {
      return {child_thphi, child_thdelta};
    } else {
      return {child_thdelta, child_thphi};
    }
  }

 private:
  const SearchResult& FrontResult() const { return results_[idx_.front()]; }

  // <PnDn>
  constexpr PnDn GetPn() const {
    if (or_node_) {
      return GetPhi();
    } else {
      return GetDelta();
    }
  }
  constexpr PnDn GetDn() const {
    if (or_node_) {
      return GetDelta();
    } else {
      return GetPhi();
    }
  }

  constexpr PnDn GetPhi() const {
    if (idx_.empty()) {
      return kInfinitePnDn;
    }
    return FrontResult().Phi(or_node_);
  }

  constexpr PnDn GetDelta() const {
    auto [sum_delta, max_delta] = GetRawDelta();
    if (sum_delta == 0 && max_delta == 0) {
      return 0;
    }

    // 後回しにしている子局面が存在する場合、その値をδ値に加算しないと局面を過大評価してしまう。
    //
    // 例） sfen +P5l2/4+S4/p1p+bpp1kp/6pgP/3n1n3/P2NP4/3P1NP2/2P2S3/3K3L1 b RGSL2Prb2gsl3p 159
    //      1筋の合駒を考える時、玉方が合駒を微妙に変えることで読みの深さを指数関数的に大きくできてしまう
    if (mp_.size() > idx_.size()) {
      // 読みの後回しが原因の（半）無限ループを回避できればいいので、1点減点しておく
      sum_delta += 1;
    }

    return sum_delta + max_delta;
  }

  constexpr std::pair<PnDn, PnDn> GetRawDelta() const {
    if (idx_.empty()) {
      return {0, 0};
    }

    const auto& best_result = FrontResult();
    // 差分計算用の値を予め持っているので、高速に計算できる
    auto sum_delta = sum_delta_except_best_;
    auto max_delta = max_delta_except_best_;
    if (sum_mask_[idx_.front()]) {
      sum_delta += best_result.Delta(or_node_);
    } else {
      max_delta = std::max(max_delta, best_result.Delta(or_node_));
    }

    return {sum_delta, max_delta};
  }

  constexpr PnDn GetSecondPhi() const {
    if (idx_.size() <= 1) {
      return kInfinitePnDn;
    }
    const auto& second_best_result = results_[idx_[1]];
    return second_best_result.Phi(or_node_);
  }

  PnDn NewThdeltaForBestMove(PnDn thdelta) const {
    PnDn delta_except_best = sum_delta_except_best_;
    if (sum_mask_[idx_[0]]) {
      delta_except_best += max_delta_except_best_;
    }

    // 計算の際はオーバーフローに注意
    if (thdelta >= delta_except_best) {
      return Clamp(thdelta - delta_except_best);
    }

    return 0;
  }
  // </PnDn>

  constexpr void RecalcDelta() {
    sum_delta_except_best_ = 0;
    max_delta_except_best_ = 0;

    for (decltype(idx_.size()) i = 1; i < idx_.size(); ++i) {
      const auto i_raw = idx_[i];
      const auto& result = results_[i_raw];
      if (sum_mask_[i_raw]) {
        sum_delta_except_best_ += result.Delta(or_node_);
      } else {
        max_delta_except_best_ = std::max(max_delta_except_best_, result.Delta(or_node_));
      }
    }
  }

  SearchResult GetProvenResult(const Node& n) const {
    if (or_node_) {
      const auto& result = FrontResult();
      const auto best_move = mp_[idx_[0]];
      const auto proof_hand = BeforeHand(n.Pos(), best_move, result.GetHand());
      const auto mate_len = std::min(result.Len() + 1, kMaxMateLen);
      const auto amount = result.Amount();

      return SearchResult::MakeFinal<true>(proof_hand, mate_len, amount);
    } else {
      // 子局面の証明駒の極小集合を計算する
      HandSet set{ProofHandTag{}};
      MateLen mate_len = kZeroMateLen;
      std::uint32_t amount = 1;
      for (const auto i_raw : idx_) {
        const auto& result = results_[i_raw];

        set.Update(result.GetHand());
        amount = std::max(amount, result.Amount());
        if (MateLen{result.Len()} + 1 > mate_len) {
          mate_len = std::min(MateLen{result.Len()} + 1, kMaxMateLen);
        }
      }

      const auto proof_hand = set.Get(n.Pos());

      // amount の総和を取ると値が大きくなりすぎるので子の数だけ足す
      amount += std::max(mp_.size(), std::size_t{1}) - 1;

      if (idx_.empty()) {
        mate_len = MateLen::Make(0, static_cast<std::uint32_t>(CountHand(n.OrHand())));
        if (mate_len > len_) {
          return SearchResult::MakeFinal<false>(n.OrHand(), mate_len.Prec(), amount);
        }
      }
      return SearchResult::MakeFinal<true>(proof_hand, mate_len, amount);
    }
  }

  SearchResult GetDisprovenResult(const Node& n) const {
    // children_ は千日手エントリが手前に来るようにソートされているので、以下のようにして千日手判定ができる
    if (!mp_.empty()) {
      if (const auto& result = FrontResult(); result.GetFinalData().is_repetition) {
        return SearchResult::MakeFinal<false, false>(n.OrHand(), len_, 1);
      }
    }

    // フツーの不詰
    if (or_node_) {
      // 子局面の反証駒の極大集合を計算する
      HandSet set{DisproofHandTag{}};
      MateLen mate_len = kMaxMateLen;
      std::uint32_t amount = 1;
      for (const auto i_raw : idx_) {
        const auto& result = results_[i_raw];
        const auto child_move = mp_[i_raw];

        set.Update(BeforeHand(n.Pos(), child_move, result.GetHand()));
        amount = std::max(amount, result.Amount());
        if (result.Len() + 1 < mate_len) {
          mate_len = result.Len() + 1;
        }
      }
      amount += std::max(mp_.size(), std::size_t{1}) - 1;
      const auto disproof_hand = set.Get(n.Pos());

      return SearchResult::MakeFinal<false>(disproof_hand, mate_len, amount);
    } else {
      const auto& result = FrontResult();
      auto disproof_hand = result.GetHand();
      const auto mate_len = std::min(result.Len() + 1, kMaxMateLen);
      const auto amount = result.Amount();

      // 駒打ちならその駒を持っていないといけない
      if (const auto best_move = mp_[idx_[0]]; is_drop(best_move)) {
        const auto pr = move_dropped_piece(best_move);
        const auto pr_cnt = hand_count(MergeHand(n.OrHand(), n.AndHand()), pr);
        const auto disproof_pr_cnt = hand_count(disproof_hand, pr);
        if (pr_cnt - disproof_pr_cnt <= 0) {
          // もし現局面の攻め方の持ち駒が disproof_hand だった場合、打とうとしている駒 pr が攻め方に独占されているため
          // 受け方は BestMove() を着手することができない。そのため、攻め方の持ち駒を何枚か受け方に渡す必要がある。
          sub_hand(disproof_hand, pr, disproof_pr_cnt);
          add_hand(disproof_hand, pr, pr_cnt - 1);
        }
      }

      return SearchResult::MakeFinal<false>(disproof_hand, mate_len, amount);
    }
  }

  SearchResult GetUnknownResult(const Node& n) const {
    const auto& result = FrontResult();
    const std::uint32_t amount = result.Amount() + mp_.size() / 2;

    Key parent_board_key{kNullKey};
    Hand parent_hand{kNullHand};
    if (parent_ != nullptr) {
      parent_board_key = parent_->board_key_;
      parent_hand = parent_->or_hand_;
    }

    UnknownData unknown_data{false, parent_board_key, parent_hand, ~sum_mask_.Value()};
    return SearchResult::MakeUnknown(GetPn(), GetDn(), or_hand_, len_, amount, unknown_data);
  }

  void ResortFront() {
    const auto comparer = MakeComparer();

    auto itr = std::lower_bound(idx_.begin() + 1, idx_.end(), idx_[0], comparer);
    std::rotate(idx_.begin(), idx_.begin() + 1, itr);
  }

  void EliminateDoubleCount(tt::TranspositionTable& tt, const Node& n) {
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
    const auto best_move = mp_[idx_[0]];
    const auto result = FrontResult();
    const auto pn = result.Pn();
    const auto dn = result.Dn();
    const auto child_board_key = children_board_key_[idx_[0]];
    const auto child_hand = AfterHand(n.Pos(), best_move, n.OrHand());

    if (!result.IsFinal()) {
      const auto parent_board_key = result.GetUnknownData().parent_board_key;
      const auto parent_hand = result.GetUnknownData().parent_hand;
      if (parent_board_key != kNullKey && parent_hand != kNullHand && parent_board_key != board_key_) {
        const detail::Edge edge{parent_board_key, child_board_key, parent_hand, child_hand, pn, dn};
        if (auto res = detail::FindKnownAncestor(tt, n, edge)) {
          SetBranchRootMaxFlag(res->first, res->second);
        }
      }
    }
  }

  void SetBranchRootMaxFlag(const detail::Edge& edge, bool branch_root_is_or_node) {
    if (board_key_ == edge.board_key && or_hand_ == edge.hand) {
      // 現局面が edge の分岐元。すなわち、edge と NthChild(0) が子孫局面が合流していることが分かった。
      // 何もケアしないと二重カウントが発生してしまうので、sum ではなく max でδ値を計算させるようにする。

      for (decltype(idx_.size()) i = 1; i < idx_.size(); ++i) {
        if (children_board_key_[idx_[i]] == edge.child_board_key) {
          sum_mask_.Reset(idx_[0]);
          if (sum_mask_[idx_[i]]) {
            sum_mask_.Reset(idx_[i]);
            RecalcDelta();
          }

          break;
        }
      }
      return;
    }

    if (branch_root_is_or_node == or_node_) {
      const auto& child_result = results_[idx_[0]];
      // max child でδ値が max_delta よりも小さい場合、NthChild(0) のδ値は上位ノードに伝播していない。
      // つまり、親局面でδ値の二重カウントは発生しないため、return できる。
      if (!sum_mask_[idx_[0]] && child_result.Delta(or_node_) < max_delta_except_best_) {
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

  const bool or_node_;
  const MovePicker mp_;
  const DelayedMoveList delayed_move_list_;
  const ChildrenBoardKey children_board_key_;
  const MateLen len_;

  ChildrenCache* const parent_;
  const Key board_key_;
  const Hand or_hand_;

  std::array<SearchResult, kMaxCheckMovesPerNode> results_;
  std::array<tt::Query, kMaxCheckMovesPerNode> queries_;

  bool does_have_old_child_{false};

  PnDn sum_delta_except_best_;
  PnDn max_delta_except_best_;

  BitSet64 sum_mask_;
  detail::IndexTable idx_;
};
}  // namespace komori

#endif  // KOMORI_NEW_CC_HPP_
