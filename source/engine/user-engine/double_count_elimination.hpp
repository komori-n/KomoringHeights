/**
 * @file double_count_elimination.hpp
 */
#ifndef KOMORI_DOUBLE_COUNT_HPP_
#define KOMORI_DOUBLE_COUNT_HPP_

#include <optional>

#include "board_key_hand_pair.hpp"
#include "node.hpp"
#include "transposition_table.hpp"
#include "typedefs.hpp"

namespace komori {

/**
 * @brief 二重カウント回避のために局面を過去方向へ遡るとき、目をつぶる pn/dn の差
 *
 * 例えば、以下のような探索経路を考える。最初の OR node で dn 値の二重カウントが疑われている。
 * 現局面 current から探索路を逆順にたどり、本当に二重カウントが発生しているかどうかを判定したい。
 *
 * ```
 * double count?                                  current(左へさかのぼりたい)
 *      |                                            |
 *      v                                            v
 *   OR node --> AND node --> OR node --> ... --> AND node --> ... --> AND node
 *           \                                                      /
 *            -> AND node --> ...                                  -
 * ```
 *
 * 例えば、経路中（分岐元除く）に以下のような OR node が 1 つでも含まれていれば、dn の二重カウントによる影響は小さい。
 * なぜなら、この OR node の dn 値は注目している（二重カウントが疑われる）パスではなく、別の局面の影響を強く
 * 受けているためである。
 *
 * ```
 *         path
 *          |
 *          v
 * OR node --> AND node
 *         \
 *          -> AND node (dn is very big)
 * ```
 *
 * 上記は OR node における dn 値二重化カウント問題を考えたが、AND node における pn 値二重カウントに関しても同様である。
 *
 * `kAncestorSearchThreshold` は、このような「支流」の存在をどれだけ許容するかを制御する値である。すなわち、親子間の
 * pn/dn 値の差が `kAncestorSearchThreshold` より大きいときは二重カウントではないとみなす。
 *
 * `kAncestorSearchThreshold` の値が小さいほど二重カウント判定が厳しくなる。そのため、二重カウントの検出漏れが
 * 発生しやすくなる。一方、`kAncestorSearchThreshold` の値が大きいほど二重カウント判定が緩くなるため、二重カウントでない
 * パスを二重カウントと誤判定して探索性能の劣化に繋がる。
 */
constexpr inline PnDn kAncestorSearchThreshold = 2 * kPnDnUnit;
/**
 * @brief 二重カウントの可能性がある辺。 FindKnownAncestor の戻り値に用いる。
 *
 * 下図のように、合流する有向路の分岐元が `branch_root_key_hand_pair`、置換表を上方向へ辿った際に
 * `branch_root_key_hand_pair` の直前で参照していたノードが `child_key_hand_pair` である。
 *
 * ```
 *        branch_root_key_hand_pair -->  Node
 *         ^                            /    \    |
 *         | child_key_hand_pair -->  Node  Node  |
 *         |                           |     |    |
 * TT Path |                           .     .    | Current Search Path
 *         |                           |     |    |
 *         |                           |   Node   |
 *         |                           \    /     |
 *         |                            Node      v
 * ```
 *
 * @see FindKnownAncestor
 */
struct BranchRootEdge {
  BoardKeyHandPair branch_root_key_hand_pair;  ///< 分岐元局面の盤面ハッシュ値と持ち駒
  BoardKeyHandPair child_key_hand_pair;        ///< 分岐元局面の子の盤面ハッシュ値と持ち駒
  bool branch_root_is_or_node;                 ///< 分岐元局面が OR node なら true
};

/**
 * @brief `n` を `move` した局面から置換表をたどると `n` の祖先に行き着くかどうか調べる
 * @param tt 置換表
 * @param n  現局面
 * @param move 次の手
 * @return 見つけた場合はその分岐元の辺。なければ `std::nullopt`。
 *
 * `n` を `move` で進めた局面を起点に、`tt` に書かれた親局面をたどる。たどった結果 `n` の先祖局面に合流するかどうかを
 * 判定し、見つけた合流元局面から分岐する辺 `BranchRootEdge` を返す。イメージ図を以下に示す
 *
 * ```
 *                               Node
 *         ^ BranchRootEdge --> /    \               |
 *         |                  Node  Node             |
 *         |                   |     |               |
 * TT Path |                   .     .               | Current Search Path
 *         |                   |     |               |
 *         |                   |   Node <-- n        |
 *         |                   \    / <-- move       |
 *         |                    Node                 v
 * ```
 */
constexpr inline std::optional<BranchRootEdge> FindKnownAncestor(tt::TranspositionTable& tt, const Node& n, Move move) {
  BoardKeyHandPair key_hand_pair = n.BoardKeyHandPairAfter(move);
  PnDn last_pn = kInfinitePnDn;
  PnDn last_dn = kInfinitePnDn;

  bool pn_flag = true;  // pn を二重カウントしている可能性
  bool dn_flag = true;  // dn を二重カウントしている可能性
  bool or_node = n.IsOrNode();

  // 万が一無限ループになったら怖いので、現在の深さを上限にループする
  for (Depth i = 0; i < n.GetDepth() && (pn_flag || dn_flag); ++i, or_node = !or_node) {
    const auto query = tt.BuildQueryByKey(key_hand_pair);
    PnDn pn{1};
    PnDn dn{1};
    const auto parent_opt = query.LookUpParent(pn, dn);
    if (parent_opt == std::nullopt) {
      break;
    }

    const BoardKeyHandPair parent_key_hand_pair = *parent_opt;  // NOLINT(bugprone-unchecked-optional-access)
    // 初回の親局面が現局面に一致するなら、そもそも二重カウントの疑いはない
    if (i == 0 && parent_key_hand_pair == n.GetBoardKeyHandPair()) {
      break;
    }

    if (n.ContainsInPath(parent_key_hand_pair.board_key, parent_key_hand_pair.hand)) {
      if ((or_node && dn_flag) || (!or_node && pn_flag)) {
        // OR node なら dn、AND node なら pn を二重カウントしている
        return BranchRootEdge{parent_key_hand_pair, key_hand_pair, or_node};
      } else {
        break;
      }
    }

    if (dn <= kAncestorSearchThreshold || (!or_node && dn > last_dn + kAncestorSearchThreshold)) {
      dn_flag = false;
    }
    if (pn <= kAncestorSearchThreshold || (or_node && pn > last_pn + kAncestorSearchThreshold)) {
      pn_flag = false;
    }

    key_hand_pair = parent_key_hand_pair;
    last_pn = pn;
    last_dn = dn;
  }

  return std::nullopt;
}
}  // namespace komori

#endif  // KOMORI_DOUBLE_COUNT_HPP_
