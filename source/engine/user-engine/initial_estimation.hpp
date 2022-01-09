#ifndef PNDN_ESTIMATION_HPP_
#define PNDN_ESTIMATION_HPP_

#include "node.hpp"
#include "typedefs.hpp"

namespace komori {
namespace detail {
inline std::pair<PnDn, PnDn> InitialPnDnPlusOrNode(const Position& n, Move move) {
  PnDn pn = 1;
  PnDn dn = 1;

  Color us = n.side_to_move();
  Color them = ~us;
  Square to = to_sq(move);
  auto attack_support = n.attackers_to(us, to).pop_count();
  auto defence_support = n.attackers_to(them, to).pop_count();

  if (defence_support >= 2) {
    // たくさん受け駒が利いている場合は後回し
    pn++;
  }

  if (attack_support + (is_drop(move) ? 1 : 0) > defence_support) {
    // 攻め駒がたくさんあるときは探索を優先する
    dn++;
  } else if (auto captured_pc = n.piece_on(to); captured_pc != NO_PIECE) {
    auto captured_pr = raw_type_of(captured_pc);
    if (captured_pr == GOLD || captured_pr == SILVER) {
      dn++;
    } else {
      pn++;
    }
  } else {
    pn++;
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
    return {2, 1};
  }

  if (!is_drop(move) && from_sq(move) == king_sq) {
    // 玉を動かす手はそこそこ価値が高い
    return {1, 1};
  }

  auto attack_support = n.attackers_to(them, to).pop_count();
  auto defence_support = n.attackers_to(us, to).pop_count();

  if (attack_support < defence_support + (is_drop(move) ? 1 : 0)) {
    return {2, 1};
  }
  return {1, 2};
}
}  // namespace detail

#if defined(USE_DEEP_DFPN)
/// deep df-pn のテーブルを初期化する。
void DeepDfpnInit(Depth d, double e);
/// 深さ depth のみ探索ノードの pn, dn の初期値を返す
PnDn InitialDeepPnDn(Depth depth);
#endif

template <bool kOrNode>
inline std::pair<PnDn, PnDn> InitialPnDn(const Node& n, Move move) {
#if defined(USE_DFPN_PLUS)
  // df-pn+
  // 評価関数の設計は GPS 将棋を参考にした。
  // https://gps.tanaka.ecc.u-tokyo.ac.jp/cgi-bin/viewvc.cgi/trunk/osl/std/osl/checkmate/libertyEstimator.h?view=markup

  if constexpr (kOrNode) {
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
}  // namespace komori

#endif  // PNDN_ESTIMATION_HPP_