#ifndef PROOF_HAND_HPP_
#define PROOF_HAND_HPP_

#include <algorithm>
#include <array>

#include "typedefs.hpp"

namespace komori {
/// hand から pr を消す
inline void RemoveHand(Hand& hand, PieceType pr) {
  hand = static_cast<Hand>(hand & ~PIECE_BIT_MASK2[pr]);
}

/// 2 つの持ち駒を 1 つにまとめる
inline Hand MergeHand(Hand h1, Hand h2) {
  return static_cast<Hand>(h1 + h2);
}

/// 先後の持ち駒（盤上にない駒）を全てかき集める
inline Hand CollectHand(const Position& n) {
  return MergeHand(n.hand_of(BLACK), n.hand_of(WHITE));
}

/// 持ち駒の枚数
inline int CountHand(Hand hand) {
  int count = 0;
  for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
    count += hand_count(hand, pr);
  }
  return count;
}

/// move 後の手駒を返す
Hand AfterHand(const Position& n, Move move, Hand before_hand);
/// move 後の手駒が after_hand のとき、移動前の持ち駒を返す
Hand BeforeHand(const Position& n, Move move, Hand after_hand);

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
Hand RemoveIfHandGivesOtherChecks(const Position& n, Hand disproof_hand);

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
Hand AddIfHandGivesOtherEvasions(const Position& n, Hand proof_hand);

/// HandSet の初期化時に使うタグ
struct ProofHandTag {};
struct DisproofHandTag {};

/// 持ち駒集合を扱うクラス。駒の種別ごとに別の変数で保存しているので、Hand を直接扱うよりもやや高速に処理できる。
///
///      |証明駒 |反証駒
/// -----+-------------
/// 初期化| ZERO | FULL
/// 更新  | |=   | &=
class HandSet {
 public:
  explicit HandSet(ProofHandTag) : proof_hand_{true}, val_{} {}
  explicit HandSet(DisproofHandTag) : proof_hand_{false} {
    for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
      val_[pr] = PIECE_BIT_MASK2[pr];
    }
  }

  HandSet() = delete;
  HandSet(const HandSet&) = default;
  HandSet(HandSet&&) noexcept = default;
  HandSet& operator=(const HandSet&) = default;
  HandSet& operator=(HandSet&&) noexcept = default;
  ~HandSet() = default;

  Hand Get(const Position& n) const {
    std::uint32_t x = 0;
    for (std::size_t pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
      x |= val_[pr];
    }

    auto hand = static_cast<Hand>(x);
    if (proof_hand_) {
      return AddIfHandGivesOtherEvasions(n, hand);
    } else {
      return RemoveIfHandGivesOtherChecks(n, hand);
    }
  }

  void Update(Hand hand) {
    if (proof_hand_) {
      for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
        val_[pr] = std::max(val_[pr], hand_exists(hand, pr));
      }
    } else {
      for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
        val_[pr] = std::min(val_[pr], hand_exists(hand, pr));
      }
    }
  }

 private:
  bool proof_hand_;
  std::array<std::uint32_t, PIECE_HAND_NB> val_;
};
}  // namespace komori

#endif  // PROOF_HAND_HPP_
