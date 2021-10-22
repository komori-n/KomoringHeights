﻿#if !defined(USER_ENGINE)
#define USER_ENGINE
#endif

#include "komoring_heights.hpp"
#include "../../types.h"

#include <algorithm>

#include "../../extra/all.h"

namespace {
using komori::PnDn;
using komori::TTEntry;
using komori::internal::ChildNodeCache;
using komori::internal::kMaxCheckMovesPerNode;
using komori::internal::PositionMateKind;

constexpr std::size_t kCacheLineSize = 64;
constexpr std::size_t kCacheLineMask = kCacheLineSize - 1;
constexpr std::size_t kDefaultHashSizeMb = 1024;
constexpr std::size_t kHashfullCalcClusters = 100;

/// 何局面読んだら generation を進めるか
constexpr std::uint32_t kNumSearchedPerGeneration = 128;
/// FirstSearch の初期深さ。数値実験してみた感じたと、1 ではあまり効果がなく、3 だと逆に遅くなるので
/// 2 ぐらいがちょうどよい
constexpr std::size_t kFirstSearchOrDepth = 1;
constexpr std::size_t kFirstSearchAndDepth = 2;

/// val 以上の 2 の累乗数を返す
template <typename T>
T RoundUpToPow2(T val) {
  T ans{1};
  while (ans < val) {
    ans <<= 1;
  }
  return ans;
}

template <typename T>
constexpr std::size_t LogCeil(T val) {
  std::size_t ret = 0;
  T x = 1;
  while (val > x) {
    ret++;
    x <<= 1;
  }
  return ret;
}

/// 現在の探索局面数に対応する generation を計算する
constexpr std::uint32_t Generation(std::uint64_t num_searched) {
  return 1 + static_cast<std::uint32_t>(num_searched / kNumSearchedPerGeneration);
}

/// c 側の sq にある pt の利き先の Bitboard を返す
Bitboard StepEffect(PieceType pt, Color c, Square sq) {
  switch (pt) {
    case PAWN:
    case LANCE:
      return pawnEffect(c, sq);
    case KNIGHT:
      return knightEffect(c, sq);
    case SILVER:
      return silverEffect(c, sq);
    case GOLD:
    case PRO_PAWN:
    case PRO_LANCE:
    case PRO_KNIGHT:
    case PRO_SILVER:
      return goldEffect(c, sq);
    case KING:
    case HORSE:
    case DRAGON:
    case QUEEN:
      return kingEffect(sq);
    case BISHOP:
      return bishopStepEffect(sq);
    case ROOK:
      return rookStepEffect(sq);
    default:
      return {};
  }
}

// -----------------------------------------------------------------------------
// <Hand関連>

/// hand から pr を消す
void RemoveHand(Hand& hand, PieceType pr) {
  hand = static_cast<Hand>(hand & ~PIECE_BIT_MASK2[pr]);
}

/// 2 つの持ち駒を 1 つにまとめる
Hand MergeHand(Hand h1, Hand h2) {
  return static_cast<Hand>(h1 + h2);
}

/// 先後の持ち駒（盤上にない駒）を全てかき集める
Hand CollectHand(const Position& n) {
  return MergeHand(n.hand_of(BLACK), n.hand_of(WHITE));
}

/// move 後の手駒が after_hand のとき、移動前の持ち駒を返す
Hand BeforeHand(const Position& n, Move move, Hand after_hand) {
  if (is_drop(move)) {
    auto pr = move_dropped_piece(move);
    add_hand(after_hand, pr);
    // オーバーフローしてしまった場合はそっと戻しておく
    if (after_hand & HAND_BORROW_MASK) {
      sub_hand(after_hand, pr);
    }
  } else {
    auto to_pc = n.piece_on(to_sq(move));
    if (to_pc != NO_PIECE) {
      auto pr = raw_type_of(to_pc);
      if (hand_exists(after_hand, pr)) {
        sub_hand(after_hand, pr);
      }
    }
  }
  return after_hand;
}

/// 持ち駒集合を扱うクラス。駒の種別ごとに別の変数で保存しているので、Hand を直接扱うよりもやや高速に処理できる。
class HandSet {
 public:
  static constexpr HandSet Zero() { return HandSet{HAND_ZERO}; }
  static constexpr HandSet Full() { return HandSet{static_cast<Hand>(HAND_BIT_MASK)}; }

  HandSet() = delete;
  HandSet(const HandSet&) = default;
  HandSet(HandSet&&) noexcept = default;
  HandSet& operator=(const HandSet&) = default;
  HandSet& operator=(HandSet&&) noexcept = default;
  ~HandSet() = default;

  Hand Get() const {
    std::uint32_t x = 0;
    for (std::size_t pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
      x |= val_[pr];
    }
    return static_cast<Hand>(x);
  }

  /// 持ち駒集合が hand 以下になるように減らす
  HandSet& operator&=(Hand hand) {
    for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
      val_[pr] = std::min(val_[pr], hand_exists(hand, pr));
    }
    return *this;
  }

  /// 持ち駒集合が hand 以上になるように増やす
  HandSet& operator|=(Hand hand) {
    for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
      val_[pr] = std::max(val_[pr], hand_exists(hand, pr));
    }
    return *this;
  }

 private:
  constexpr explicit HandSet(Hand hand) noexcept : val_{} {
    for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
      val_[pr] = hand & PIECE_BIT_MASK2[pr];
    }
  }

  std::array<std::uint32_t, PIECE_HAND_NB> val_;
};

/**
 * @brief 局面 n の子局面がすべて 反証駒 disproof_hand で不詰であることが既知の場合、もとの局面 n の反証駒を計算する。
 *
 * OrNode の時に限り呼び出せる。
 * disproof_hand をそのまま返すのが基本だが、もし disproof_hand の中に局面 n では持っていない駒が含まれていた場合、
 * その駒を打つ手を初手とした詰みがあるかもしれない。（局面 n に含まれないので、前提となる子局面の探索には含まれない）
 * そのため、現局面で持っていない種別の持ち駒がある場合は、反証駒から消す必要がある。
 *
 * ### 例
 *
 * 後手の持駒：飛二 角二 金四 銀四 桂三 香三 歩十六
 *   ９ ８ ７ ６ ５ ４ ３ ２ １
 * +---------------------------+
 * | ・ ・ ・ ・ ・ ・ ・ ・v玉|一
 * | ・ ・ ・ ・ ・ ・ ・ ・ ・|二
 * | ・ ・ ・ ・ ・ ・ ・ ・ 歩|三
 * | ・ ・ ・ ・ ・ ・ ・ ・ ・|四
 * | ・ ・ ・ ・ ・ ・ ・ ・ ・|五
 * | ・ ・ ・ ・ ・ ・ ・ ・ ・|六
 * | ・ ・ ・ ・ ・ ・ ・ ・ ・|七
 * | ・ ・ ・ ・ ・ ・ ・ ・ ・|八
 * | ・ ・ ・ ・ ・ ・ ・ ・ ・|九
 * +---------------------------+
 * 先手の持駒：桂 香 歩
 *
 * ↑子局面はすべて金を持っていても詰まないが、現局面で金を持っているなら詰む
 *
 * @param n 現在の局面
 * @param disproof_hand n に対する子局面の探索で得られた反証駒の極大集合
 * @return Hand disproof_hand から n で持っていない　かつ　王手になる持ち駒を除いた持ち駒
 */
Hand RemoveIfHandGivesOtherChecks(const Position& n, Hand disproof_hand) {
  Color us = n.side_to_move();
  Color them = ~n.side_to_move();
  Hand hand = n.hand_of(us);
  Square king_sq = n.king_square(them);
  auto droppable_bb = ~n.pieces();

  for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
    if (!hand_exists(hand, pr)) {
      // 二歩の場合は反証駒を消す必要はない（打てないので）
      if (pr == PAWN && (n.pieces(us, PAWN) & FILE_BB[file_of(king_sq)])) {
        continue;
      }

      if (droppable_bb.test(StepEffect(pr, them, king_sq))) {
        // pr を持っていたら王手ができる -> pr は反証駒から除かれるべき
        RemoveHand(disproof_hand, pr);
      }
    }
  }
  return disproof_hand;
}

/**
 * @brief 局面 n の子局面がすべて証明駒 proof_hand で詰みであることが既知の場合、もとの局面 n の証明駒を計算する。
 *
 * AndNode の時に限り呼び出せる。
 * proof_hand をそのまま返すのが基本だが、もし proof_hand の中に局面 n では持っていない駒が含まれていた場合、
 * その駒を打って合駒をすれば詰みを防げたかもしれない。（局面 n に含まれないので、前提となる子局面の探索には含まれない）
 * そのため、現局面で持っていない種別の持ち駒がある場合は、証明駒に加える（合駒がなかった情報を付与する）必要がある。
 *
 * ### 例
 *
 * 後手の持駒：香
 *   ９ ８ ７ ６ ５ ４ ３ ２ １
 * +---------------------------+
 * | ・ ・ ・ ・ ・ ・ ・ ・ ・|一
 * | ・ ・ ・ ・ ・ ・ ・ ・ ・|二
 * | ・ ・ ・ ・ ・ ・ ・ ・ ・|三
 * | ・ ・ ・ ・ ・ ・ ・v香 ・|四
 * |v金v銀v角v桂 ・ ・ ・v歩v玉|五
 * |v角v飛v飛v桂 ・ ・ ・v香 ・|六
 * |v桂v桂v金 ・ ・ ・ ・ ・ ・|七
 * |v銀v銀v銀 ・ ・ ・ ・ と ・|八
 * |v金v金 ・ ・ ・ ・ ・ ・ 香|九
 * +---------------------------+
 * 先手の持駒：歩十六
 *
 * ↑後手の合駒が悪いので詰んでしまう。つまり、「先手が歩を独占している」という情報も証明駒に含める必要がある。
 *
 * @param n 現在の局面
 * @param proof_hand n に対する子局面の探索で得られた証明駒の極小集合
 * @return Hand proof_hand から n で受け方が持っていない　かつ　合駒で王手を防げる持ち駒を攻め方側に集めた持ち駒
 */
Hand AddIfHandGivesOtherEvasions(const Position& n, Hand proof_hand) {
  auto us = n.side_to_move();
  auto them = ~us;
  Hand us_hand = n.hand_of(us);
  Hand them_hand = n.hand_of(them);
  auto king_sq = n.king_square(n.side_to_move());
  auto checkers = n.checkers();

  if (checkers.pop_count() == 1) {
    auto checker_sq = checkers.pop();
    if (between_bb(king_sq, checker_sq)) {
      // 駒を持っていれば合駒で詰みを防げたかもしれない（合法手が増えるから）
      for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
        if (pr == PAWN) {
          bool double_pawn = true;
          auto bb = between_bb(king_sq, checker_sq);
          while (bb) {
            auto sq = bb.pop();
            if (!(n.pieces(us, PAWN) & FILE_BB[file_of(sq)])) {
              double_pawn = false;
              break;
            }
          }

          if (double_pawn) {
            continue;
          }
        }

        if (!hand_exists(us_hand, pr)) {
          // pr を持っていれば詰みを防げた（かもしれない）
          RemoveHand(proof_hand, pr);
          proof_hand = MergeHand(proof_hand, static_cast<Hand>(hand_exists(them_hand, pr)));
        }
      }
    }
  }
  return proof_hand;
}
// </Hand関連>
// -----------------------------------------------------------------------------

/// 詰将棋専用 MovePicker
template <bool kOrNode, bool kOrdering = false>
class MovePicker {
 public:
  MovePicker() = delete;
  MovePicker(const MovePicker&) = delete;
  MovePicker(MovePicker&&) = delete;
  MovePicker& operator=(const MovePicker&) = delete;
  MovePicker& operator=(MovePicker&&) = delete;
  ~MovePicker() = default;

  explicit MovePicker(const Position& n) {
    bool judge_check = false;
    if constexpr (kOrNode) {
      if (n.in_check()) {
        last_ = generateMoves<EVASIONS_ALL>(n, move_list_.data());
        // 逆王手になっているかチェックする必要がある
        judge_check = true;
      } else {
        last_ = generateMoves<CHECKS_ALL>(n, move_list_.data());
      }
    } else {
      last_ = generateMoves<EVASIONS_ALL>(n, move_list_.data());
    }

    // OrNodeで王手ではない手と違法手を取り除く
    last_ = std::remove_if(move_list_.data(), last_,
                           [&](const auto& m) { return (judge_check && !n.gives_check(m.move)) || !n.legal(m.move); });

    // オーダリング情報を付加したほうが定数倍速くなる
    if constexpr (kOrdering) {
      auto us = n.side_to_move();
      auto them = ~us;
      auto king_color = kOrNode ? them : us;
      auto king_sq = n.king_square(king_color);
      for (ExtMove* itr = move_list_.data(); itr != last_; ++itr) {
        const auto& move = itr->move;
        auto to = to_sq(move);
        auto attackers_to_us = n.attackers_to(us, to);
        auto attackers_to_them = n.attackers_to(them, to);
        auto pt = type_of(n.moved_piece_before(move));
        itr->value = 0;

        // 成れるのに成らない
        if (!is_drop(move) && !is_promote(move)) {
          auto from = from_sq(move);
          if ((pt == PAWN || pt == BISHOP || pt == ROOK)) {
            itr->value += 1000;  // 歩、角、飛車を成らないのは大きく減点する（打ち歩詰めの時以外は考える必要ない）
          }
        }

        itr->value -= 10 * dist(king_sq, to);

        if constexpr (kOrNode) {
          itr->value -= 2 * (attackers_to_us.pop_count() + is_drop(move)) - attackers_to_them.pop_count();
          itr->value -= pt;
        } else {
          if (pt == KING) {
            itr->value -= 15;
          }
          itr->value += attackers_to_them.pop_count() - 2 * attackers_to_us.pop_count();
        }
      }
    }
  }

  std::size_t size() const { return static_cast<std::size_t>(last_ - move_list_.data()); }
  ExtMove* begin() { return move_list_.data(); }
  ExtMove* end() { return last_; }
  bool empty() const { return size() == 0; }

 private:
  std::array<ExtMove, kMaxCheckMovesPerNode> move_list_;
  ExtMove* last_;
};

/// OrNode で move が近接王手となるか判定する
bool IsStepCheck(const Position& n, Move move) {
  auto us = n.side_to_move();
  auto them = ~us;
  auto king_sq = n.king_square(them);
  Piece pc = n.moved_piece_after(move);
  PieceType pt = type_of(pc);

  return StepEffect(pt, us, to_sq(move)).test(king_sq);
}
}  // namespace

namespace komori {
namespace internal {

template <bool kOrNode>
MoveSelector<kOrNode>::MoveSelector(const Position& n, TranspositionTable& tt, Depth depth, PnDn th_sum_n)
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
    child.generation = entry->Generation();
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
void MoveSelector<kOrNode>::Update() {
  // 各子局面のエントリを更新する
  for (std::size_t i = 0; i < std::min(children_len_, std::size_t{3}); ++i) {
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
    child.generation = entry->Generation();

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
        entry->MarkDeleteCandidate();
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
    return children_[children_len_ - 1].generation == kRepetitionDisproven;
  } else {
    return children_[0].generation == kRepetitionDisproven;
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
            << " generation=" << entry->Generation() << sync_endl;  //
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
  std::unordered_map<Key, MateMove> memo;

  MateMove mate_move = SearchPv<true>(memo, p, 0);

  std::vector<Move> result;
  std::array<StateInfo, internal::kMaxNumMateMoves> st_info;
  auto st_info_p = st_info.data();
  // 探索メモをたどって詰手順を復元する
  while (mate_move.move != MOVE_NONE) {
    result.push_back(mate_move.move);
    p.do_move(mate_move.move, *st_info_p++);

    Key key = p.key();
    if (result.size() >= internal::kMaxNumMateMoves || memo.find(key) == memo.end()) {
      break;
    }

    mate_move = memo[key];
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
    auto output = FirstSearch<kOrNode>(n, depth, kOrNode ? kFirstSearchOrDepth : kFirstSearchAndDepth);
    if (output.first != PositionMateKind::kUnknown) {
      return;
    }
  }

  parents.insert(n.key());
  // スタックの消費を抑えめために、ローカル変数で確保する代わりにメンバで動的確保した領域を探索に用いる
  internal::MoveSelector<kOrNode>* selector = nullptr;
  if constexpr (kOrNode) {
    selector = &or_selectors_.emplace_back(n, tt_, depth, thdn);
  } else {
    selector = &and_selectors_.emplace_back(n, tt_, depth, thpn);
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
    selector->Update();
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

template <bool kOrNode>
DfPnSearcher::FirstSearchOutput DfPnSearcher::FirstSearch(Position& n, Depth depth, Depth remain_depth) {
  // OrNode は高速一手詰めルーチンで高速に詰みかどうか判定できる
  if (kOrNode && !n.in_check()) {
    if (auto move = Mate::mate_1ply(n); move != MOVE_NONE) {
      HandSet proof_hand = HandSet::Zero();
      auto query = tt_.GetQuery<kOrNode>(n, depth);
      auto curr_hand = n.hand_of(n.side_to_move());

      StateInfo st_info;
      n.do_move(move, st_info);
      proof_hand |= BeforeHand(n, move, AddIfHandGivesOtherEvasions(n, HAND_ZERO));
      n.undo_move(move);

      proof_hand &= curr_hand;
      query.SetProven(proof_hand.Get());
      return std::make_pair(PositionMateKind::kProven, proof_hand.Get());
    }
  }

  MovePicker<kOrNode> move_picker{n};
  if (move_picker.empty()) {
    auto query = tt_.GetQuery<kOrNode>(n, depth);
    if constexpr (kOrNode) {
      Hand disproof_hand = RemoveIfHandGivesOtherChecks(n, CollectHand(n));
      query.SetDisproven(disproof_hand);
      return std::make_pair(PositionMateKind::kDisproven, disproof_hand);
    } else {
      auto proof_hand = AddIfHandGivesOtherEvasions(n, HAND_ZERO);
      query.SetProven(proof_hand);
      return std::make_pair(PositionMateKind::kProven, proof_hand);
    }
  }

  if (remain_depth <= 1) {
    // 一手詰ではなく探索深さが残っていて かつ 指し手の選択肢がある
    // => この局面は残り1手で詰まないし、不詰でもないので探索しなくてよい
    return std::make_pair(PositionMateKind::kUnknown, HAND_ZERO);
  }

  if constexpr (kOrNode) {
    auto ret = PositionMateKind::kDisproven;
    HandSet disproof_hand = HandSet::Full();
    for (const auto& move : move_picker) {
      auto child_query = tt_.GetChildQuery<kOrNode>(n, move.move, depth + 1);
      auto child_entry = child_query.LookUpWithoutCreation();
      FirstSearchOutput output;
      if (child_entry->IsProvenNode()) {
        output.first = PositionMateKind::kProven;
        output.second = child_entry->ProperHand(AfterHand(n, move.move, n.hand_of(n.side_to_move())));
      } else if (child_entry->IsNonRepetitionDisprovenNode()) {
        output.first = PositionMateKind::kDisproven;
        output.second = child_entry->ProperHand(AfterHand(n, move.move, n.hand_of(n.side_to_move())));
      } else {
        // 近接王手以外は時間の無駄なので無視する
        if (!IsStepCheck(n, move)) {
          ret = PositionMateKind::kUnknown;
          continue;
        }

        StateInfo st_info;
        n.do_move(move.move, st_info);
        output = FirstSearch<!kOrNode>(n, depth + 1, remain_depth - 1);
        n.undo_move(move.move);
      }

      if (output.first == PositionMateKind::kProven) {
        auto query = tt_.GetQuery<kOrNode>(n, depth);
        auto proof_hand = BeforeHand(n, move.move, output.second);
        query.SetProven(proof_hand);
        return std::make_pair(PositionMateKind::kProven, proof_hand);
      }

      if (ret == PositionMateKind::kDisproven && output.first == PositionMateKind::kDisproven) {
        disproof_hand &= BeforeHand(n, move.move, output.second);
      } else if (output.first == PositionMateKind::kUnknown) {
        ret = PositionMateKind::kUnknown;
      }
    }

    if (ret == PositionMateKind::kDisproven) {
      disproof_hand |= n.hand_of(n.side_to_move());
      auto hand = RemoveIfHandGivesOtherChecks(n, disproof_hand.Get());
      auto query = tt_.GetQuery<kOrNode>(n, depth);
      query.SetDisproven(hand);

      return std::make_pair(ret, hand);
    }
  } else {  // kOrNode == false
    auto ret = PositionMateKind::kProven;
    HandSet proof_hand = HandSet::Zero();
    for (const auto& move : move_picker) {
      auto child_query = tt_.GetChildQuery<kOrNode>(n, move.move, depth + 1);
      auto child_entry = child_query.LookUpWithoutCreation();
      FirstSearchOutput output;
      if (child_entry->IsProvenNode()) {
        output.first = PositionMateKind::kProven;
        output.second = child_entry->ProperHand(n.hand_of(~n.side_to_move()));
      } else if (child_entry->IsNonRepetitionDisprovenNode()) {
        output.first = PositionMateKind::kDisproven;
        output.second = child_entry->ProperHand(n.hand_of(~n.side_to_move()));
      } else {
        StateInfo st_info;
        n.do_move(move.move, st_info);
        output = FirstSearch<!kOrNode>(n, depth + 1, remain_depth - 1);
        n.undo_move(move.move);
      }

      if (output.first == PositionMateKind::kDisproven) {
        auto query = tt_.GetQuery<kOrNode>(n, depth);
        query.SetDisproven(output.second);
        return std::make_pair(PositionMateKind::kDisproven, output.second);
      }

      if (ret == PositionMateKind::kProven && output.first == PositionMateKind::kProven) {
        proof_hand |= output.second;
      } else if (output.first == PositionMateKind::kUnknown) {
        ret = PositionMateKind::kUnknown;
      }
    }

    if (ret == PositionMateKind::kProven) {
      proof_hand &= n.hand_of(~n.side_to_move());
      auto query = tt_.GetQuery<kOrNode>(n, depth);
      auto hand = AddIfHandGivesOtherEvasions(n, proof_hand.Get());
      query.SetProven(hand);

      return std::make_pair(ret, proof_hand.Get());
    }
  }
  return std::make_pair(PositionMateKind::kUnknown, HAND_ZERO);
}

template <bool kOrNode>
DfPnSearcher::MateMove DfPnSearcher::SearchPv(std::unordered_map<Key, MateMove>& memo, Position& n, Depth depth) {
  auto key = n.key();
  if (auto itr = memo.find(key); itr != memo.end()) {
    if (itr->second.kind != PositionMateKind::kProven) {
      itr->second.repetition_start = key;
    }
    return itr->second;
  }

  memo[key] = {};
  // 子局面の LookUp はやや重いので、OrNode の場合は高速一手詰めルーチンで early return できないか試してみる
  if (kOrNode && !n.in_check()) {
    if (auto move = Mate::mate_1ply(n); move != MOVE_NONE) {
      return memo[key] = {PositionMateKind::kProven, move, 1, 0};
    }
  }

  MovePicker<kOrNode> move_picker{n};
  if (!kOrNode && move_picker.empty()) {
    return memo[key] = {PositionMateKind::kProven, MOVE_NONE, 0, 0};
  }

  // OrNode では最短で詰むように、AndNode では最長で詰むように手を選ぶ
  memo[key].num_moves = kOrNode ? internal::kMaxNumMateMoves : 0;
  Key repetition_start = 0;
  for (const auto& move : move_picker) {
    auto child_query = tt_.GetChildQuery<kOrNode>(n, move.move, depth + 1);
    auto child_entry = child_query.LookUpWithoutCreation();
    if (!child_entry->IsProvenNode()) {
      continue;
    }

    StateInfo state_info;
    n.do_move(move.move, state_info);
    MateMove child_mate_move = SearchPv<!kOrNode>(memo, n, depth + 1);
    n.undo_move(move.move);

    if (child_mate_move.kind == PositionMateKind::kProven) {
      if ((kOrNode && memo[key].num_moves > child_mate_move.num_moves + 1) ||
          (!kOrNode && memo[key].num_moves < child_mate_move.num_moves + 1)) {
        memo[key].num_moves = child_mate_move.num_moves + 1;
        memo[key].move = move.move;
      }
    } else if (child_mate_move.repetition_start != 0 && child_mate_move.repetition_start != key) {
      repetition_start = child_mate_move.repetition_start;
    }
  }

  if (memo[key].num_moves != (kOrNode ? internal::kMaxNumMateMoves : 0)) {
    memo[key].kind = PositionMateKind::kProven;
    repetition_start = 0;
    memo[key].repetition_start = 0;
  }

  if (repetition_start == 0) {
    return memo[key];
  } else {
    memo.erase(key);
    return {PositionMateKind::kUnknown, MOVE_NONE, 0, repetition_start};
  }
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
template DfPnSearcher::MateMove DfPnSearcher::SearchPv<true>(std::unordered_map<Key, MateMove>& memo,
                                                             Position& n,
                                                             Depth depth);
template DfPnSearcher::MateMove DfPnSearcher::SearchPv<false>(std::unordered_map<Key, MateMove>& memo,
                                                              Position& n,
                                                              Depth depth);
}  // namespace komori