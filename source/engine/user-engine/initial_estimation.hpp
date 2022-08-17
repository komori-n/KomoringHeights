#ifndef KOMORI_PNDN_ESTIMATION_HPP_
#define KOMORI_PNDN_ESTIMATION_HPP_

#include <utility>

#include "node.hpp"
#include "typedefs.hpp"

namespace komori {
namespace detail {
inline constexpr PnDn kPnDnUnit = 2;

inline std::pair<PnDn, PnDn> InitialPnDnPlusOrNode(const Position& n, Move move) {
  PnDn pn = kPnDnUnit;
  PnDn dn = kPnDnUnit;

  Color us = n.side_to_move();
  Color them = ~us;
  Square to = to_sq(move);
  auto attack_support = n.attackers_to(us, to).pop_count();
  auto defence_support = n.attackers_to(them, to).pop_count();

  if (defence_support >= 2) {
    // たくさん受け駒が利いている場合は後回し
    pn += kPnDnUnit;
  }

  if (attack_support + (is_drop(move) ? 1 : 0) > defence_support) {
    // 攻め駒がたくさんあるときは探索を優先する
    dn += kPnDnUnit;
  } else if (auto captured_pc = n.piece_on(to); captured_pc != NO_PIECE) {
    auto captured_pr = raw_type_of(captured_pc);
    if (captured_pr == GOLD || captured_pr == SILVER) {
      dn += kPnDnUnit;
    } else {
      pn += kPnDnUnit;
    }
  } else {
    pn += kPnDnUnit;
  }

  return {pn, dn};
}

inline std::pair<PnDn, PnDn> InitialPnDnPlusAndNode(const Position& n, Move move) {
  Color us = n.side_to_move();
  Color them = ~us;
  Square to = to_sq(move);
  Square king_sq = n.king_square(us);

  if (n.piece_on(to) != NO_PIECE) {
    // コマを取る手は探索を優先する
    return {2 * kPnDnUnit, 1 * kPnDnUnit};
  }

  if (!is_drop(move) && from_sq(move) == king_sq) {
    // 玉を動かす手はそこそこ価値が高い
    return {1 * kPnDnUnit, 1 * kPnDnUnit};
  }

  auto attack_support = n.attackers_to(them, to).pop_count();
  auto defence_support = n.attackers_to(us, to).pop_count();

  if (attack_support < defence_support + (is_drop(move) ? 1 : 0)) {
    return {2 * kPnDnUnit, 1 * kPnDnUnit};
  }
  return {1 * kPnDnUnit, 2 * kPnDnUnit};
}
}  // namespace detail

#if defined(USE_DEEP_DFPN)
/// deep df-pn のテーブルを初期化する。
void DeepDfpnInit(Depth d, double e);
/// 深さ depth のみ探索ノードの pn, dn の初期値を返す
PnDn InitialDeepPnDn(Depth depth);
#endif

inline std::pair<PnDn, PnDn> InitialPnDn(const Node& n, Move move) {
#if defined(USE_DFPN_PLUS)
  // df-pn+
  // 評価関数の設計は GPS 将棋を参考にした。
  // https://gps.tanaka.ecc.u-tokyo.ac.jp/cgi-bin/viewvc.cgi/trunk/osl/std/osl/checkmate/libertyEstimator.h?view=markup

  bool or_node = n.IsOrNode();
  if (or_node) {
    return detail::InitialPnDnPlusOrNode(n.Pos(), move);
  } else {
    return detail::InitialPnDnPlusAndNode(n.Pos(), move);
  }
#elif defined(USE_DEEP_DFPN)
  PnDn pndn = InitialDeepPnDn(n.GetDepth());
  return {pndn, pndn};
#else
  return {kInitialPn, kInitialDn};
#endif
}

/**
 * @brief 局面 n の手 move に対するざっくりとした評価値を返す。
 *
 * 値が小さければ小さいほど（手番側にとって）良い手を表す。
 * MovePicker において指し手のオーダリングをする際に使う。
 */
inline int MoveBriefEvaluation(const Node& n, Move move) {
  // 駒のざっくりとした価値。
  constexpr int kPtValues[] = {
      0, 1, 2, 2, 3, 5, 5, 5, 8, 5, 5, 5, 5, 8, 8, 8,
  };

  auto us = n.Us();
  auto king_color = n.AndColor();
  auto king_sq = n.Pos().king_square(king_color);
  auto to = to_sq(move);

  int value = 0;

  // 成れるのに成らない
  if (!is_drop(move) && !is_promote(move)) {
    auto from = from_sq(move);
    auto before_pt = type_of(n.Pos().moved_piece_before(move));
    if ((before_pt == PAWN || before_pt == BISHOP || before_pt == ROOK) &&
        (enemy_field(us).test(from) || enemy_field(us).test(to))) {
      value += 100;  // 歩、角、飛車を成らないのは大きく減点する（打ち歩詰めの時以外は考える必要ない）
    }
  }

  auto after_pt = type_of(n.Pos().moved_piece_after(move));
  value -= kPtValues[after_pt];
  value += dist(king_sq, to);

  return value;
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
inline bool IsSumDeltaNode(const Node& n, Move move) {
  bool or_node = n.IsOrNode();
  if (is_drop(move)) {
    // 駒打ち
    if (or_node) {
      if (move_dropped_piece(move) == LANCE || move_dropped_piece(move) == BISHOP || move_dropped_piece(move) == ROOK) {
        // 飛車と角はだいたいどこから打っても同じ
        return false;
      }
    }
  }

  return true;
}
}  // namespace komori

#endif  // KOMORI_PNDN_ESTIMATION_HPP_
