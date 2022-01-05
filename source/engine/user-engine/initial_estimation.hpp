#ifndef PNDN_ESTIMATION_HPP_
#define PNDN_ESTIMATION_HPP_

#include "typedefs.hpp"
#include "node.hpp"

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
}  // namespace komori

#endif  // PNDN_ESTIMATION_HPP_