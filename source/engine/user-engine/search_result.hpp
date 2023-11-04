/**
 * @file search_result.hpp
 */
#ifndef KOMORI_SEARCH_RESULT_HPP_
#define KOMORI_SEARCH_RESULT_HPP_

#include <utility>

#include "bitset.hpp"
#include "mate_len.hpp"
#include "typedefs.hpp"

namespace komori {
/// 結論が出ていないノード（Unknown）の探索結果
struct UnknownData {
  // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
  bool is_first_visit;  ///< 初めて訪れた局面かどうか
  BitSet64 sum_mask;    ///< δ値を和で計算すべき子の集合
  // NOLINTEND(misc-non-private-member-variables-in-classes)
};

/// 結論が出てたノード（Final）の探索結果
struct FinalData {
  // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
  Depth repetition_start;  ///< 千日手の開始深さ。千日手でないなら kDepthMax。
  Hand hand;               ///< 証明駒 or 反証駒
  // NOLINTEND(misc-non-private-member-variables-in-classes)

  /// 千日手かどうか
  constexpr bool IsRepetition() const noexcept { return repetition_start < kDepthMax; }
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
   * @param len           探索時の残り手数
   * @param amount        探索量
   * @param unknown_data  Unknown部分の結果
   */
  static constexpr SearchResult MakeUnknown(PnDn pn,
                                            PnDn dn,
                                            MateLen len,
                                            SearchAmount amount,
                                            UnknownData unknown_data) {
    return {pn, dn, len, amount, unknown_data};
  }

  /**
   * @brief Finalな探索結果で初期化する
   * @tparam kIsProven     `true` なら詰み、`false` なら不詰
   * @param hand           攻め方の持ち駒
   * @param len            探索時の残り手数
   * @param amount         探索量
   *
   * 千日手の場合、この関数ではなく `MakeRepetition()` を用いること。
   */
  template <bool kIsProven>
  static constexpr SearchResult MakeFinal(Hand hand, MateLen len, SearchAmount amount) {
    const auto pn = kIsProven ? 0 : kInfinitePnDn;
    const auto dn = kIsProven ? kInfinitePnDn : 0;
    return {pn, dn, len, amount, FinalData{kDepthMax, hand}};
  }

  /**
   * @brief 千日手の探索結果で初期化する
   * @param hand 攻め方の持ち駒
   * @param len  探索時の残り手数
   * @param amount 探索量
   * @param rep_start 千日手の開始局面。この深さを下回ったら千日手は解消されたと判断する。
   */
  static constexpr SearchResult MakeRepetition(Hand hand, MateLen len, SearchAmount amount, Depth rep_start) {
    return {kInfinitePnDn, 0, len, amount, FinalData{rep_start, hand}};
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
  /// 探索時の残り手数
  constexpr MateLen Len() const { return len_; }
  /// 探索量
  constexpr SearchAmount Amount() const { return amount_; }
  /// Unknown部分の結果。`!IsFinal()` の場合のみ呼び出し可能。
  constexpr const UnknownData& GetUnknownData() const { return unknown_data_; }
  /// Final部分の結果。`IsFinal()` の場合のみ呼び出し可能。
  constexpr const FinalData& GetFinalData() const { return final_data_; }

  /// 探索結果に基づくノード状態（詰み／不詰／千日手／不明）
  constexpr NodeState GetNodeState() const {
    if (Pn() == 0) {
      return NodeState::kProven;
    } else if (Dn() == 0) {
      if (!GetFinalData().IsRepetition()) {
        return NodeState::kDisproven;
      } else {
        return NodeState::kRepetition;
      }
    } else {
      return NodeState::kUnknown;
    }
  }

  /// `result` を出力ストリームへ出力する。
  friend std::ostream& operator<<(std::ostream& os, const SearchResult& result) {
    os << "{";
    if (result.IsFinal()) {
      const auto final_data = result.GetFinalData();
      if (result.Pn() == 0) {
        os << "proof_hand=" << final_data.hand;
      } else {
        if (!final_data.IsRepetition()) {
          os << "disproof_hand=" << final_data.hand;
        } else {
          os << "repetition start=" << final_data.repetition_start;
        }
      }
    } else {
      os << "(pn,dn)=(" << result.pn_ << "," << result.dn_ << ")";
    }

    os << " len=" << result.len_;
    os << " amount=" << result.amount_;
    os << "}";
    return os;
  }

  /**
   * @brief `result` をもとにしきい値 `thpn`, `thdn` を大きくして探索を延長する（TCA）
   * @param result[in] 現在の探索結果
   * @param thpn[inout] pnの探索しきい値
   * @param thdn[inout] dnの探索しきい値
   * @note `result.IsFinal() == true` ならしきい値を更新しないので注意。
   */
  friend constexpr void ExtendSearchThreshold(const SearchResult& result, PnDn& thpn, PnDn& thdn) noexcept {
    if (!result.IsFinal()) {
      if (result.Pn() < kInfinitePnDn) {
        thpn = ClampPnDn(thpn, result.Pn() + 1);
      }

      if (result.Dn() < kInfinitePnDn) {
        thdn = ClampPnDn(thdn, result.Dn() + 1);
      }
    }
  }

 private:
  /// Unknown用の初期化関数。`MakeUnknown()` を使ってほしいので private に隠しておく。
  constexpr SearchResult(PnDn pn, PnDn dn, MateLen len, SearchAmount amount, UnknownData unknown_data)
      : pn_{pn}, dn_{dn}, len_{len}, amount_{amount}, unknown_data_{unknown_data} {}
  /// Final用の初期化関数。`MakeFinal()` を使ってほしいので private に隠しておく。
  constexpr SearchResult(PnDn pn, PnDn dn, MateLen len, SearchAmount amount, FinalData final_data)
      : pn_{pn}, dn_{dn}, len_{len}, amount_{amount}, final_data_{final_data} {}

  PnDn pn_;              ///< pn
  PnDn dn_;              ///< dn
  MateLen len_;          ///< 探索時の残り手数
  SearchAmount amount_;  ///< 探索量
  union {
    UnknownData unknown_data_;  ///< Unknown専用領域
    FinalData final_data_;      ///< Final専用領域
  };  ///< メモリを節約するために `UnknownData` と `FinalData` を同じ領域に押し込む
};

/**
 * @brief `SearchResult` 同士の半順序関係。
 *
 * φ値、δ値、千日手の有無を元にした `SearchResult` 同士の（狭義の）半順序関係を定義する。`operator()` の呼び出しにより、
 * 任意の 2 つの `SearchResult` のどちらがより「良い」かを調べることができる。自身の勝ちに近い探索結果（直感的には
 * φ値がより小さい探索結果）ほど Less と判定される。
 *
 * 結果は `SearchResultComparer::Ordering` により返却される。詳しくは enum class の定義を参照。
 */
class SearchResultComparer {
 public:
  /**
   * @brief （狭義の）半順序状態
   */
  enum class Ordering {
    kEquivalent,  ///< 等しい(a == b)
    kLess,        ///< 小さい(a < b)
    kGreater,     ///< 大きい(a > b)
  };

  /**
   * @brief `SearchResultComparer` の初期化を行う
   * @param or_node OR node なら true, AND node なら false.
   */
  explicit constexpr SearchResultComparer(bool or_node) noexcept : or_node_{or_node} {}
  /// Copy constructor(default)
  constexpr SearchResultComparer(const SearchResultComparer&) noexcept = default;
  /// Move constructor(default)
  constexpr SearchResultComparer(SearchResultComparer&&) noexcept = default;
  /// Copy assign operator(delete)
  constexpr SearchResultComparer& operator=(const SearchResultComparer&) noexcept = delete;
  /// Move assign operator(delete)
  constexpr SearchResultComparer& operator=(SearchResultComparer&&) noexcept = delete;
  /// Destructor(default)
  ~SearchResultComparer() noexcept = default;

  /**
   * @brief `lhs` と `rhs` の比較を行う
   * @param lhs `SearchResult`
   * @param rhs `searchResult`
   * @return `Ordering`
   *
   * `lhs` と `rhs` の比較は以下の基準で行う。
   *
   * 1. φ値が異なるならその値の大小で比較
   * 2. δ値が異なるならその値の大小で比較
   * 3. 片方が千日手でもう片方が普通の不詰なら、
   *   a. OR node では千日手の方を優先（Lessとする）
   *   b. AND node では普通の不詰を優先（Lessとする）
   * 4. Equivalent を返す
   */
  constexpr Ordering operator()(const SearchResult& lhs, const SearchResult& rhs) const noexcept {
    if (lhs.Phi(or_node_) < rhs.Phi(or_node_)) {
      return Ordering::kLess;
    } else if (lhs.Phi(or_node_) > rhs.Phi(or_node_)) {
      return Ordering::kGreater;
    } else if (lhs.Delta(or_node_) < rhs.Delta(or_node_)) {
      return Ordering::kLess;
    } else if (lhs.Delta(or_node_) > rhs.Delta(or_node_)) {
      return Ordering::kGreater;
    }

    if (lhs.Pn() == 0 /* && rhs.Pn() == 0 */) {
      if (lhs.Len() < rhs.Len()) {
        return or_node_ ? Ordering::kLess : Ordering::kGreater;
      } else if (lhs.Len() > rhs.Len()) {
        return or_node_ ? Ordering::kGreater : Ordering::kLess;
      }
    }

    if (lhs.Dn() == 0 /* && rhs.Dn() == 0 */) {
      const auto l_rep_start = lhs.GetFinalData().repetition_start;
      const auto r_rep_start = rhs.GetFinalData().repetition_start;

      if (l_rep_start != r_rep_start) {
        // OR node では repetition_start が小さい順、AND node では repetition_start が大きい順に並べたい
        if (!or_node_ ^ (l_rep_start < r_rep_start)) {
          return Ordering::kLess;
        } else {
          return Ordering::kGreater;
        }
      }
    }

    if (lhs.Amount() != rhs.Amount()) {
      if (lhs.Amount() < rhs.Amount()) {
        return Ordering::kLess;
      } else {
        return Ordering::kGreater;
      }
    }

    return Ordering::kEquivalent;
  }

 private:
  const bool or_node_;  ///< 現局面が OR node なら true, AND node なら false.
};

}  // namespace komori

#endif  // KOMORI_SEARCH_RESULT_HPP_
