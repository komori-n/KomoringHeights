#ifndef TTENTRY_HPP_
#define TTENTRY_HPP_

#include "typedefs.hpp"

namespace komori {

/// Position の探索情報を格納するための構造体
class TTEntry {
 public:
  TTEntry() = default;
  TTEntry(std::uint32_t hash_high, ::Hand hand, PnDn pn, PnDn dn, ::Depth depth);

  /// (hand, depth) に一致しているか、`hand` を証明／反証できる内容なら true
  bool ExactOrDeducable(::Hand hand, ::Depth depth) const;
  /// 探索深さが depth 以下で hand の優等局面なら true（渡された hand よりも詰みに近いはず）
  bool IsSuperiorAndShallower(::Hand hand, ::Depth depth) const;
  /// 探索深さが depth 以下で hand の劣等局面なら true（渡された hand よりも不詰に近いはず）
  bool IsInferiorAndShallower(::Hand hand, ::Depth depth) const;

  /// 現在の証明数
  PnDn Pn() const;
  /// 現在の反証数
  PnDn Dn() const;
  /// 証明数と反証数を更新する
  void Update(PnDn pn, PnDn dn, std::uint64_t num_searched);

  /// 詰みかどうか
  bool IsProvenNode() const;
  /// 不詰（千日手含む）かどうか
  bool IsDisprovenNode() const;
  /// 千日手ではない不詰かどうか
  bool IsNonRepetitionDisprovenNode() const;
  /// 千日手による不詰かどうか
  bool IsRepetitionDisprovenNode() const;
  /// 詰みまたは不詰（千日手）かどうか。いずれでもない場合のみ false。
  bool IsProvenOrDisprovenNode() const;

  /// entry の内容をもとに、持ち駒 `hand` を持っていれば詰みだと言えるなら true、それ以外なら false
  bool DoesProve(::Hand hanD) const;
  /// entry の内容をもとに、持ち駒 `hand` を持っていれば不詰だと言えるなら true、それ以外なら false
  bool DoesDisprove(::Hand hand) const;

  /// 証明駒 `hand` による詰みを報告する
  void SetProven(::Hand hand);
  /// 反証駒 `hand` による不詰を報告する
  void SetDisproven(::Hand hand);
  /// 千日手による不詰を報告する
  void SetRepetitionDisproven();

  auto Generation() const { return generation_; }
  bool IsFirstVisit() const;
  void MarkDeleteCandidate();

  auto HashHigh() const { return hash_high_; }
  auto Hand() const { return hand_; }

 private:
  std::uint32_t hash_high_;  ///< board_keyの上位32bit
  ::Hand hand_;              ///< 攻め方のhand。pn==0なら証明駒、dn==0なら反証駒を表す。
  PnDn pn_, dn_;             ///< pn, dn。直接参照禁止。
  ::Depth depth_;  ///< 探索深さ。千日手回避のためにdepthが違う局面は別局面として扱う
  komori::Generation generation_;  ///< 探索世代。古いものから順に上書きされる
};

}  // namespace komori

#endif  // TTENTRY_HPP_