/**
 * @file initial_estimation.hpp
 */
#ifndef KOMORI_PNDN_ESTIMATION_HPP_
#define KOMORI_PNDN_ESTIMATION_HPP_

#include <random>
#include <utility>

#include "node.hpp"
#include "typedefs.hpp"

namespace komori {
namespace detail {
/// 駒のざっくりとした価値。スレッドごとに微妙に乱数を加えたいので thread_local にしている。
thread_local inline int kPtValues[] = {
    0, 10, 20, 20, 30, 50, 50, 50, 80, 50, 50, 50, 50, 80, 80, 80,
};

/// df-pn+ で用いるパラメータたち
struct DfpnPlusParameters {
  PnDn or_pn_base = kPnDnUnit;
  PnDn or_dn_base = kPnDnUnit;
  PnDn or_defense = kPnDnUnit;
  PnDn or_support = kPnDnUnit;
  PnDn or_capture_gold_silver = kPnDnUnit;
  PnDn or_capture_others = kPnDnUnit;
  PnDn or_others = kPnDnUnit;

  PnDn and_capture_pn = 2 * kPnDnUnit;
  PnDn and_capture_dn = kPnDnUnit;
  PnDn and_king_pn = kPnDnUnit;
  PnDn and_king_dn = kPnDnUnit;
  PnDn and_good_pn = 2 * kPnDnUnit;
  PnDn and_good_dn = kPnDnUnit;
  PnDn and_bad_pn = kPnDnUnit;
  PnDn and_bad_dn = 2 * kPnDnUnit;
};

/// df-pn+ で用いるパラメータ。スレッドごとに微妙に乱数を加えたいので thread_local にしている。
thread_local inline DfpnPlusParameters kDfpnPlusParameters;
}  // namespace detail

/**
 * @brief 初期評価値を乱数でずらす
 * @param thread_id スレッド番号
 */
inline void InitBriefEvaluation(std::uint32_t thread_id) {
  // デフォルトで設定しているパラメータはシングルスレッド版の（ほぼ）最適値なので、乱数を加える必要はない
  if (thread_id != 0) {
    std::mt19937 mt(thread_id);
    // kPtValues と kDfpnPlusParameters へ薄い乱数を加える

    std::normal_distribution<double> dist(0, 200);
    for (auto& value : detail::kPtValues) {
      value += static_cast<int>(dist(mt));
    }

    std::discrete_distribution<PnDn> discrete_dist({0.7, 0.25, 0.05});
    // pn_base と dn_base はいじらないほうが強そう
    // detail::kDfpnPlusParameters.or_pn_base += discrete_dist(mt);
    // detail::kDfpnPlusParameters.or_dn_base += discrete_dist(mt);
    detail::kDfpnPlusParameters.or_defense += discrete_dist(mt);
    detail::kDfpnPlusParameters.or_support += discrete_dist(mt);
    detail::kDfpnPlusParameters.or_capture_gold_silver += discrete_dist(mt);
    detail::kDfpnPlusParameters.or_capture_others += discrete_dist(mt);
    detail::kDfpnPlusParameters.or_others += discrete_dist(mt);

    detail::kDfpnPlusParameters.and_capture_pn += discrete_dist(mt);
    detail::kDfpnPlusParameters.and_capture_dn += discrete_dist(mt);
    detail::kDfpnPlusParameters.and_king_pn += discrete_dist(mt);
    detail::kDfpnPlusParameters.and_king_dn += discrete_dist(mt);
    detail::kDfpnPlusParameters.and_good_pn += discrete_dist(mt);
    detail::kDfpnPlusParameters.and_good_pn += discrete_dist(mt);
    detail::kDfpnPlusParameters.and_bad_dn += discrete_dist(mt);
    detail::kDfpnPlusParameters.and_bad_dn += discrete_dist(mt);
  }
}

namespace detail {
/**
 * @brief df-pn+における OR node の pn/dn 初期値を計算する
 * @param n     現局面
 * @param move  次の手
 * @return `n` を `move` で動かした局面の pn/dn の初期値
 */
inline std::pair<PnDn, PnDn> InitialPnDnPlusOrNode(const Position& n, Move move) {
  PnDn pn = kDfpnPlusParameters.or_pn_base;
  PnDn dn = kDfpnPlusParameters.or_dn_base;

  const Color us = n.side_to_move();
  const Color them = ~us;
  const Square to = to_sq(move);
  // n.attackers_to(color, to) はそこそこ重い処理なので、n.attackers_to(to) の結果を使い回すことで高速化できる
  const auto attacker_bb = n.attackers_to(to);
  const auto attack_support = (attacker_bb & n.pieces(us)).pop_count();
  const auto defense_support = (attacker_bb & n.pieces(them)).pop_count();

  if (defense_support >= 2) {
    // たくさん受け駒が利いている場合は後回し
    pn += kDfpnPlusParameters.or_defense;
  }

  if (attack_support + (is_drop(move) ? 1 : 0) > defense_support) {
    // 攻め駒がたくさんあるときは探索を優先する
    dn += kDfpnPlusParameters.or_support;
  } else if (auto captured_pc = n.piece_on(to); captured_pc != NO_PIECE) {
    const auto captured_pr = raw_type_of(captured_pc);
    if (captured_pr == GOLD || captured_pr == SILVER) {
      dn += kDfpnPlusParameters.or_capture_gold_silver;
    } else {
      pn += kDfpnPlusParameters.or_capture_others;
    }
  } else {
    pn += kDfpnPlusParameters.or_others;
  }

  return {pn, dn};
}

/**
 * @brief df-pn+における AND node の pn/dn 初期値を計算する
 * @param n     現局面
 * @param move  次の手
 * @return `n` を `move` で動かした局面の pn/dn の初期値
 */
inline std::pair<PnDn, PnDn> InitialPnDnPlusAndNode(const Position& n, Move move) {
  const Color us = n.side_to_move();
  const Color them = ~us;
  const Square to = to_sq(move);
  const Square king_sq = n.king_square(us);

  if (n.piece_on(to) != NO_PIECE) {
    // コマを取る手は探索を優先する
    return {kDfpnPlusParameters.and_capture_pn, kDfpnPlusParameters.and_capture_dn};
  }

  if (!is_drop(move) && from_sq(move) == king_sq) {
    // 玉を動かす手はそこそこ価値が高い
    return {kDfpnPlusParameters.and_king_pn, kDfpnPlusParameters.and_king_dn};
  }

  const auto attacker_bb = n.attackers_to(to);
  const auto attack_support = (attacker_bb & n.pieces(them)).pop_count();
  const auto defense_support = (attacker_bb & n.pieces(us)).pop_count();

  if (attack_support < defense_support + (is_drop(move) ? 1 : 0)) {
    return {kDfpnPlusParameters.and_good_pn, kDfpnPlusParameters.and_good_dn};
  }
  return {kDfpnPlusParameters.and_bad_pn, kDfpnPlusParameters.and_bad_dn};
}
}  // namespace detail

#if defined(USE_DEEP_DFPN)
/// deep df-pn のテーブルを初期化する。
void DeepDfpnInit(Depth d, double e);
/// 深さ depth のみ探索ノードの pn, dn の初期値を返す
PnDn InitialDeepPnDn(Depth depth);
#endif

/**
 * @brief 初めて訪れた局面の pn/dn 初期値を計算する
 * @param n     現局面
 * @param move  次の手
 * @return `n` を `move` で動かした局面の pn/dn の初期値
 *
 * 局面の pn/dn 初期値を与える関数。古典的な df-pn アルゴリズムでは (pn, dn) = (1, 1) だが、この値を
 * 詰みやすさ／詰み逃れやすさに応じて増減させることで探索性能を向上させられる。
 */
inline std::pair<PnDn, PnDn> InitialPnDn(const Node& n, Move move) {
#if !defined(USE_DEEP_DFPN)
  // df-pn+
  // 評価関数の設計は GPS 将棋を参考にした。
  // https://gps.tanaka.ecc.u-tokyo.ac.jp/cgi-bin/viewvc.cgi/trunk/osl/std/osl/checkmate/libertyEstimator.h?view=markup

  const bool or_node = n.IsOrNode();
  if (or_node) {
    return detail::InitialPnDnPlusOrNode(n.Pos(), move);
  } else {
    return detail::InitialPnDnPlusAndNode(n.Pos(), move);
  }
#else   // !defined(USE_DEEP_DFPN)
  PnDn pndn = InitialDeepPnDn(n.GetDepth());
  return {pndn, pndn};
#endif  // !defined(USE_DEEP_DFPN)
}

/**
 * @brief 局面 n の手 move に対するざっくりとした評価値を返す。
 *
 * 値が小さければ小さいほど（手番側にとって）良い手を表す。
 * MovePicker において指し手のオーダリングをする際に使う。
 */
inline int MoveBriefEvaluation(const Node& n, Move move) {
  auto us = n.Us();
  auto king_sq = n.KingSquare();
  auto to = to_sq(move);

  int value = 0;

  // 成れるのに成らない
  if (!is_drop(move) && !is_promote(move)) {
    auto from = from_sq(move);
    auto before_pt = type_of(n.Pos().moved_piece_before(move));
    if ((before_pt == PAWN || before_pt == BISHOP || before_pt == ROOK) &&
        (enemy_field(us).test(from) || enemy_field(us).test(to))) {
      value += 1000;  // 歩、角、飛車を成らないのは大きく減点する（打ち歩詰めの時以外は考える必要ない）
    }
  }

  auto after_pt = type_of(n.Pos().moved_piece_after(move));
  value -= detail::kPtValues[after_pt];
  value += 10 * dist(king_sq, to);

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
  const bool or_node = n.IsOrNode();
  if (!is_drop(move)) {
    // 駒打ち以外
    if (or_node) {
      const auto from = from_sq(move);
      const auto to = to_sq(move);
      const auto pc = n.Pos().piece_on(from);
      const auto pt = type_of(pc);

      // 2 or 3段目の香成と不成
      if (pt == LANCE) {
        const auto king_sq = n.Pos().king_square(n.AndColor());
        if ((n.Us() == BLACK && (rank_of(to) == RANK_3 || rank_of(to) == RANK_2) && king_sq == to + SQ_U) ||
            (n.Us() == WHITE && (rank_of(to) == RANK_7 || rank_of(to) == RANK_8) && king_sq == to + SQ_D)) {
          return false;
        }
      }
    }
  }

  return true;
}
}  // namespace komori

#endif  // KOMORI_PNDN_ESTIMATION_HPP_
