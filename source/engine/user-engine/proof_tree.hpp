#ifndef PROOF_TREE_HPP_
#define PROOF_TREE_HPP_

#include <optional>
#include <unordered_map>

#include "node.hpp"
#include "typedefs.hpp"

namespace komori {
class TranspositionTable;

/**
 * @brief 証明木
 *
 * df-pnアルゴリズムでは、ある局面が詰むことを示すのは高速に行えるが、最善応手列（PV）を求めるのは難しい。
 * そのため、ProofTree では、各局面の詰み手数および最善手から木構造（厳密には木ではなく森）を構築する。
 */
class ProofTree {
 public:
  ProofTree() = default;
  ProofTree(const ProofTree&) = delete;
  ProofTree(ProofTree&&) = delete;
  ProofTree& operator=(const ProofTree&) = delete;
  ProofTree& operator=(ProofTree&&) = delete;
  ~ProofTree() = default;

  /// 証明木を削除する
  void Clear() { edges_.clear(); };
  /**
   * @brief 最善応手列 moves を木構造に追加する
   *
   * n は探索の開始局面（root node）である必要はない。
   * moves は最短手順である必要はないが、末端局面は詰み（手番が玉方で合法酒がない局面（でなければならない。
   *
   * @param n      pv の開始局面
   * @param moves  pv
   */
  void AddBranch(Node& n, const std::vector<Move>& moves);
  /**
   * @brief n.DoMove(move16) した局面が ProofTree に保存されているか？
   */
  bool HasEdgeAfter(Node& n, Move16 move16) const;
  /**
   * @brief 局面 n の詰み手数を返す。
   *
   * n が ProofTree に保存されていない場合、0 を返す。
   */
  Depth MateLen(Node& n) const;
  /**
   * @brief 局面 n の最善応手列（PV）を返す
   *
   * n が ProofTree に保存されていない場合と千日手により PV が指せない場合、nullopt を返す
   */
  std::optional<std::vector<Move>> GetPv(Node& n);
  /**
   * @brief 木に登録された情報を元に、局面 n の最善手を更新する
   *
   * OR node:  子局面のうち最も mate_len が小さいものを選ぶ。
   *           ただし、ProofTree に登録されていない子局面は不詰として扱う。
   * AND node: 子局面のうち最も mate_len が大きいものを選ぶ。
   *           ただし、ProofTree に登録されていない子局面は 0 手詰として扱う。
   *
   * 子局面がひとつも登録されていない場合、best_move = MOVE_NONE で、0 手詰めとして扱う。
   */
  void Update(Node& n);

  /// デバッグ用関数。現在の木構造を標準出力に出力する
  void Verbose(Node& n) const;

 private:
  struct Edge {
    /// （暫定）最善手。メモリ消費を抑えるため Move ではなく Move16 を用いる。
    Move16 best_move;
    /// 詰み手数。メモリ消費を抑えるために Depth ではなく int16_t を用いる。
    std::int16_t mate_len;

    Edge(Move16 best_move, Depth mate_len) : best_move{best_move}, mate_len{static_cast<int16_t>(mate_len)} {}
    Move BestMove(const Node& n) const { return n.Pos().to_move(best_move); }
  };

  void RollForwardAndUpdate(Node& n, const std::vector<Move>& moves);
  void RollBackAndUpdate(Node& n, const std::vector<Move>& moves);

  /// 木構造の本体
  std::unordered_map<Key, Edge> edges_{};
};
}  // namespace komori

#endif  // PROOF_TREE_HPP_
