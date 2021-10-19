#ifndef TTENTRY_HPP_
#define TTENTRY_HPP_

#include "typedefs.hpp"

namespace komori {

/// Position の探索情報を格納するための構造体
class TTEntry {
 public:
  TTEntry() = default;
  TTEntry(std::uint32_t hash_high, Hand hand, PnDn pn, PnDn dn, Depth depth);

  /// (hand, depth) に一致しているか、`hand` を証明／反証できる内容なら true
  bool ExactOrDeducable(Hand hand, Depth depth) const;
  /// 探索深さが depth 以下で hand の優等局面なら true（渡された hand よりも詰みに近いはず）
  bool IsSuperiorAndShallower(Hand hand, Depth depth) const;
  /// 探索深さが depth 以下で hand の劣等局面なら true（渡された hand よりも不詰に近いはず）
  bool IsInferiorAndShallower(Hand hand, Depth depth) const;

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
  bool DoesProve(Hand hanD) const;
  /// entry の内容をもとに、持ち駒 `hand` を持っていれば不詰だと言えるなら true、それ以外なら false
  bool DoesDisprove(Hand hand) const;

  /// Proven  -> 必要ないノードは消す
  /// Unknown -> proof_hand で証明可能ならいらない
  bool UpdateWithProofHand(Hand proof_hand);
  bool UpdateWithDisproofHand(Hand disproof_hand);

  /// 証明駒 `hand` による詰みを報告する
  void SetProven(Hand hand);
  /// 反証駒 `hand` による不詰を報告する
  void SetDisproven(Hand hand);
  /// 千日手による不詰を報告する
  void SetRepetitionDisproven();
  void AddHand(Hand hand);

  auto Generation() const { return common_.generation; }
  bool IsFirstVisit() const;
  void MarkDeleteCandidate();

  auto HashHigh() const { return common_.hash_high; }
  Hand FirstHand() const;
  Hand ProperHand(Hand hand) const;
  bool IsWritableNewProofHand() const;
  bool IsWritableNewDisproofHand() const;

 private:
  union {
    struct {
      std::uint32_t hash_high;  ///< board_keyの上位32bit
      std::array<std::uint32_t, 6> dummy;
      komori::Generation generation;  ///< 探索世代。古いものから順に上書きされる
    } common_;
    struct {
      std::uint32_t hash_high;  ///< board_keyの上位32bit
      Hand hand;                ///< 攻め方のhand。pn==0なら証明駒、dn==0なら反証駒を表す。
      PnDn pn, dn;              ///< pn, dn。直接参照禁止。
      Depth depth;  ///< 探索深さ。千日手回避のためにdepthが違う局面は別局面として扱う
      komori::Generation generation;  ///< 探索世代。古いものから順に上書きされる
    } unknown_;
    struct {
      std::uint32_t hash_high;        ///< board_keyの上位32bit
      std::array<Hand, 6> hands;      ///< 証明駒または反証駒
      komori::Generation generation;  ///< 探索世代。古いものから順に上書きされる
    } known_;
  };

  static_assert(sizeof(common_) == sizeof(unknown_));
  static_assert(sizeof(common_) == sizeof(known_));
};

}  // namespace komori

#endif  // TTENTRY_HPP_