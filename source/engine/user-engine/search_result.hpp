#ifndef KOMORI_SEARCH_RESULT_HPP_
#define KOMORI_SEARCH_RESULT_HPP_

#include <utility>

#include "mate_len.hpp"
#include "typedefs.hpp"

namespace komori {
/// 結論が出ていないノード（Unknown）の探索結果
struct UnknownData {
  bool is_first_visit;   ///< 初めて訪れた局面かどうか
  Key parent_board_key;  ///< 1手前の盤面ハッシュ値
  Hand parent_hand;      ///< 1手前の攻め方の持ち駒
  std::uint64_t secret;  ///< secret
};

/// 結論が出てたノード（Final）の探索結果
struct FinalData {
  bool is_repetition;  ///< 千日手による final かどうか
};

/**
 * @brief 探索結果をやり取りするためのクラス。
 *
 * このクラスは、置換表からの探索結果の読み込みおよび探索結果の書き込みに用いる。置換表サイズの節約および
 * 探索性能の向上のために、置換表本体とは異なるデータ構造を用いる。
 *
 * 領域を節約するために、Unknown（結論が出ていないノード）とFinal（結論が出ているノード）専用の領域を union で
 * 共有している。
 *
 * 余計な変更を阻止するために、初期化時以外はメンバを変更できないようにしている。少し煩雑になってしまうが、
 * メンバへのアクセスには必ず getter method を用いなければならない。
 */
class SearchResult {
 public:
  /**
   * @brief Unknownな探索結果で初期化する
   * @param pn            pn
   * @param dn            dn
   * @param hand          攻め方の持ち駒
   * @param len           探索時の残り手数
   * @param amount        探索量
   * @param unknown_data  Unknown部分の結果
   */
  static constexpr SearchResult MakeUnknown(PnDn pn,
                                            PnDn dn,
                                            Hand hand,
                                            MateLen len,
                                            std::uint32_t amount,
                                            UnknownData unknown_data) {
    // SearchResult{}.InitUnknown(...) だと constexpr にならないのでダメ
    return {pn, dn, hand, len, amount, unknown_data};
  }

  /**
   * @brief Finalな探索結果で初期化する
   * @tparam kIsProven     `true` なら詰み、`false` なら不詰
   * @tparam kIsRepetition `true` なら千日手による不詰
   * @param hand           攻め方の持ち駒
   * @param len            探索時の残り手数
   * @param amount         探索量
   *
   * `kIsProven` と `kIsRepetition` を同時に `true` にすることはできない。
   */
  template <bool kIsProven, bool kIsRepetition = false>
  static constexpr SearchResult MakeFinal(Hand hand, MateLen len, std::uint32_t amount) {
    static_assert(!(kIsProven && kIsRepetition));

    const auto pn = kIsProven ? 0 : kInfinitePnDn;
    const auto dn = kIsProven ? kInfinitePnDn : 0;
    return {pn, dn, hand, len, amount, FinalData{kIsRepetition}};
  }

  /// 領域を事前確保できるようにするために、デフォルト構築可能にする。
  SearchResult() = default;

  /// pn
  constexpr PnDn Pn() const { return pn_; }
  /// dn
  constexpr PnDn Dn() const { return dn_; }
  /// φ値
  constexpr PnDn Phi(bool or_node) const { return or_node ? Pn() : Dn(); }
  /// δ値
  constexpr PnDn Delta(bool or_node) const { return or_node ? Dn() : Pn(); }
  /// 詰み／不詰の結論が出ているか
  constexpr bool IsFinal() const { return Pn() == 0 || Dn() == 0; }
  /// 攻め方の持ち駒
  constexpr Hand GetHand() const { return hand_; }
  /// 探索時の残り手数
  constexpr MateLen Len() const { return len_; }
  /// 探索量
  constexpr std::uint32_t Amount() const { return amount_; }
  /// Unknown部分の結果。`!IsFinal()` の場合のみ呼び出し可能。
  constexpr const UnknownData& GetUnknownData() const { return unknown_data_; }
  /// Final部分の結果。`IsFinal()` の場合のみ呼び出し可能。
  constexpr const FinalData& GetFinalData() const { return final_data_; }

  /**
   * @brief Unknownな探索結果を上書きで保存する
   * @param pn            pn
   * @param dn            dn
   * @param hand          攻め方の持ち駒
   * @param len           探索時の残り手数
   * @param amount        探索量
   * @param unknown_data  Unknown部分の結果
   */
  constexpr void InitUnknown(PnDn pn, PnDn dn, Hand hand, MateLen len, std::uint32_t amount, UnknownData unknown_data) {
    pn_ = pn;
    dn_ = dn;
    hand_ = hand;
    len_ = len;
    amount_ = amount;
    unknown_data_ = std::move(unknown_data);
  }

  /**
   * @brief Finalな探索結果を上書きで保存する
   * @tparam kIsProven     `true` なら詰み、`false` なら不詰
   * @tparam kIsRepetition `true` なら千日手による不詰
   * @param hand           攻め方の持ち駒
   * @param len            探索時の残り手数
   * @param amount         探索量
   *
   * `kIsProven` と `kIsRepetition` を同時に `true` にすることはできない。
   */
  template <bool kIsProven, bool kIsRepetition = false>
  constexpr void InitFinal(Hand hand, MateLen len, std::uint32_t amount) {
    static_assert(!(kIsProven && kIsRepetition));

    pn_ = kIsProven ? 0 : kInfinitePnDn;
    dn_ = kIsProven ? kInfinitePnDn : 0;
    hand_ = hand;
    len_ = len;
    amount_ = amount;
    final_data_.is_repetition = kIsRepetition;
  }

  /// `result` を出力ストリームへ出力する。
  friend std::ostream& operator<<(std::ostream& os, const SearchResult& result) {
    os << "{";
    if (result.pn_ == 0) {
      os << "proof_hand=" << result.hand_;
    } else if (result.dn_ == 0) {
      if (result.final_data_.is_repetition) {
        os << "repetition";
      } else {
        os << "disproof_hand" << result.hand_;
      }
    } else {
      os << "(pn,dn)=(" << result.pn_ << "," << result.dn_ << ")";
    }

    os << " len=" << result.len_;
    os << " amount=" << result.amount_;
    os << "}";
    return os;
  }

 private:
  /// Unknown用の初期化関数。`MakeUnknown()` を使ってほしいので private に隠しておく。
  constexpr SearchResult(PnDn pn, PnDn dn, Hand hand, MateLen len, std::uint32_t amount, UnknownData unknown_data)
      : pn_{pn}, dn_{dn}, hand_{hand}, len_{len}, amount_{amount}, unknown_data_{unknown_data} {}
  /// Final用の初期化関数。`MakeFinal()` を使ってほしいので private に隠しておく。
  constexpr SearchResult(PnDn pn, PnDn dn, Hand hand, MateLen len, std::uint32_t amount, FinalData final_data)
      : pn_{pn}, dn_{dn}, hand_{hand}, len_{len}, amount_{amount}, final_data_{final_data} {}

  PnDn pn_;               ///< pn
  PnDn dn_;               ///< dn
  Hand hand_;             ///< 攻め方の持ち駒
  MateLen len_;           ///< 探索時の残り手数
  std::uint32_t amount_;  ///< 探索量
  union {
    UnknownData unknown_data_;  ///< Unknown専用領域
    FinalData final_data_;      ///< Final専用領域
  };
};

}  // namespace komori

#endif  // KOMORI_SEARCH_RESULT_HPP_
