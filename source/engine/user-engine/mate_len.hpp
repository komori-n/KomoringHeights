#ifndef KOMORI_MATE_LEN_HPP_
#define KOMORI_MATE_LEN_HPP_

#include <algorithm>

#include "typedefs.hpp"

namespace komori {
class MateLen;

/**
 * @brief 駒余り手数を加味した詰み手数（16 bit版）。
 *
 * 詰み手数を定義する。同じ手数で詰む局面同士に対し、攻め方の駒余り枚数に応じて全順序が定義されている。具体的には、
 * より手数が短く、駒余り枚数が多いほど「小さく」なるように演算子 `<` を定義している。
 *
 * ```cpp
 * EXPECT_TRUE(MateLen16::Make(26, 4) < MateLen16::Make(33, 4));
 * EXPECT_TRUE(MateLen16::Make(33, 4) < MateLen16::Make(33, 3));
 * ```
 *
 * 置換表領域を節約するために、16 bit 整数に詰め込む。そのため、駒余り枚数は [0, 16) の範囲に丸められる。
 */
class MateLen16 : DefineNotEqualByEqual<MateLen16>, DefineComparisonOperatorsByEqualAndLess<MateLen16> {
 public:
  /// `MateLen::To16()` で `Make()` を介さずに直接 `MateLen16` インスタンスを作ってもらうため `friend` にする。
  friend MateLen;

  /**
   * @brief `MateLen16` インスタンスを構築する。
   * @tparam T          整数型
   * @param len         手数
   * @param final_hand  最終局面の駒余り枚数
   * @return `MateLen16` インスタンス
   *
   * `MateLen16` インスタンスを構築する。`MateLen16` のコンストラクタは private に隠されているため、
   * インスタンスを構築するためにはこの関数を使う必要がある。
   *
   * 利用者側での `std::uint16_t` への煩わしい型変換をなくすために、template で型を受け取るインターフェースに
   * なっている。ただし、第2引数は `komori::Identity` を用いて不要な型推論を抑制している。
   */
  template <typename T>
  static constexpr MateLen16 Make(const T& len, const typename Identity<T>::type& final_hand) {
    return {static_cast<std::uint16_t>(len + 1), std::min(static_cast<std::uint16_t>(final_hand), std::uint16_t{15})};
  }
  /**
   * @brief 置換表の初期化を簡単にするため、デフォルト構築可能にする。
   *
   * デフォルト構築後は内部状態が未定義状態となるので注意。
   */
  MateLen16() = default;

  /// 手数
  constexpr std::uint16_t Len() const { return len_plus_1_ - 1; }
  /// 最終局面における駒余り枚数
  constexpr std::uint16_t FinalHand() const { return final_hand_; }

  /// `lhs` と `rhs` が等しいかどうか判定する
  constexpr friend bool operator==(const MateLen16& lhs, const MateLen16& rhs) noexcept {
    return lhs.len_plus_1_ == rhs.len_plus_1_ && lhs.final_hand_ == rhs.final_hand_;
  }

  /**
   * @brief `lhs` が `rhs` よりも小さいかどうかを判定する。
   * @param lhs 詰み手数
   * @param rhs 詰み手数
   * @return `lhs` が `rhs` と比べて小さいかどうか
   * @note 最終局面の駒余り枚数は多ければ多いほど詰み手数が「小さく」扱われる。
   */
  constexpr friend bool operator<(const MateLen16& lhs, const MateLen16& rhs) noexcept {
    if (lhs.len_plus_1_ != rhs.len_plus_1_) {
      return lhs.len_plus_1_ < rhs.len_plus_1_;
    }

    return lhs.final_hand_ > rhs.final_hand_;
  }

  /// 詰み手数に整数を加算する
  friend constexpr inline MateLen16 operator+(const MateLen16& lhs, Depth d) {
    return MateLen16{static_cast<std::uint16_t>(lhs.len_plus_1_ + d), lhs.final_hand_};
  }

  /// 詰み手数に整数を加算する
  friend constexpr inline MateLen16 operator+(Depth d, const MateLen16& rhs) { return rhs + d; }

  /// 詰み手数から整数を減算する
  friend constexpr inline MateLen16 operator-(const MateLen16& lhs, Depth d) {
    return MateLen16{static_cast<std::uint16_t>(lhs.len_plus_1_ - d), lhs.final_hand_};
  }

 private:
  /// コンストラクタ。外部から直接構築できないように private に隠す。
  constexpr MateLen16(std::uint16_t len_plus_1, std::uint16_t final_hand)
      : len_plus_1_{len_plus_1}, final_hand_{final_hand} {}

  std::uint16_t len_plus_1_ : 12;  ///< 詰み手数 + 1([0, 4096))。詰み手数に1を足すことで 0手詰めが若干扱いやすくなる。
  std::uint16_t final_hand_ : 4;  ///< 詰み局面における駒余り枚数([0, 16))
};

/// 詰み手数の最小値。
constexpr inline MateLen16 kZeroMateLen16 = MateLen16::Make(0, 15);
/// 詰み手数の最大値。
constexpr inline MateLen16 kMaxMateLen16 = MateLen16::Make(kDepthMax, 0);

/// -1手。
constexpr inline MateLen16 kMinusZeroMateLen16 = MateLen16::Make(0, 15) - 1;
/// +∞手
constexpr inline MateLen16 kInfiniteMateLen16 = MateLen16::Make(kDepthMax + 1, 0);

/**
 * @brief 駒余り手数を加味した詰み手数。
 *
 * 基本的な機能は `MateLen16` と同様。探索中は `std::uint16_t` に詰め込まれた `MateLen16` を使うより、
 * この型を使ったほうが高速に動作する。
 *
 * `MateLen16` から `MateLen` へ変換するためには、`MateLen::From` を使用する。一方、`MateLen` から `MateLen16` へ
 * 変換するためには、`To16()` を用いる。
 */
class MateLen : DefineNotEqualByEqual<MateLen>, DefineComparisonOperatorsByEqualAndLess<MateLen> {
 public:
  /// 最終局面の持ち駒枚数の最大値。
  static constexpr inline std::uint32_t kFinalHandMax = 38;
  /**
   * @brief `MateLen` インスタンスを構築する。
   *
   * @param len         手数
   * @param final_hand  最終局面の駒余り枚数
   * @return `MateLen` インスタンス
   */
  static constexpr MateLen Make(std::uint32_t len, std::uint32_t final_hand) { return {len + 1, final_hand}; }

  /// `MateLen16` から `MateLen` インスタンスを構築する。
  static constexpr MateLen From(const MateLen16& mate_len16) {
    return {mate_len16.len_plus_1_, mate_len16.final_hand_};
  }

  /// `MateLen16` と仕様を合わせるためデフォルト構築可能にする。
  MateLen() = default;

  /// 手数
  constexpr std::uint32_t Len() const { return len_plus_1_ - 1; }
  /// 最終局面における駒余り枚数
  constexpr std::uint32_t FinalHand() const { return final_hand_; }

  /**
   * @brief `MateLen16` へ変換する。
   * @return `MateLen16` インスタンス
   * @note 内部変数の型が異なるので、情報が落ちることがある。
   */
  constexpr MateLen16 To16() const {
    const auto len_16 = static_cast<std::uint16_t>(len_plus_1_);
    const auto final_16 = static_cast<std::uint16_t>(final_hand_);

    return {len_16, std::min(final_16, std::uint16_t{15})};
  }

  /**
   * @brief `*this < len` なる `len` のうち最小のものを返す。
   * @return `*this` より大きい最小の要素
   */
  constexpr MateLen Succ() const {
    if (final_hand_ == 0) {
      return {len_plus_1_ + 1, kFinalHandMax};
    } else {
      return {len_plus_1_, final_hand_ - 1};
    }
  }

  /**
   * @brief `*this < len` なる `len` のうち最小のものを返す。ただし、`this->Len() % 2 == len.Len() % 2` である。
   * @return `*this` より大きい最小の要素
   */
  constexpr MateLen Succ2() const {
    if (final_hand_ == 0) {
      return {len_plus_1_ + 2, kFinalHandMax};
    } else {
      return {len_plus_1_, final_hand_ - 1};
    }
  }

  /**
   * @brief `len < *this` なる `len` のうち最大のものを返す。
   * @return `*this` より小さい最大の要素
   */
  constexpr MateLen Prec() const {
    if (final_hand_ == kFinalHandMax) {
      return {len_plus_1_ - 1, 0};
    } else {
      return {len_plus_1_, final_hand_ + 1};
    }
  }

  /**
   * @brief `len < *this` なる `len` のうち最大のものを返す。ただし、`this->Len() % 2 == len.Len() % 2` である。
   * @return `*this` より小さい最大の要素
   */
  constexpr MateLen Prec2() const {
    if (final_hand_ == kFinalHandMax) {
      return {len_plus_1_ - 2, 0};
    } else {
      return {len_plus_1_, final_hand_ + 1};
    }
  }

  /// `lhs` と `rhs` が等しいかどうか判定する
  constexpr friend bool operator==(const MateLen& lhs, const MateLen& rhs) {
    return lhs.len_plus_1_ == rhs.len_plus_1_ && lhs.final_hand_ == rhs.final_hand_;
  }

  /// `lhs` が `rhs` がより小さいか判定する
  constexpr friend bool operator<(const MateLen& lhs, const MateLen& rhs) {
    if (lhs.len_plus_1_ != rhs.len_plus_1_) {
      return lhs.len_plus_1_ < rhs.len_plus_1_;
    }

    return lhs.final_hand_ > rhs.final_hand_;
  }

  /// 詰み手数に整数を加算する
  friend inline MateLen operator+(const MateLen& lhs, Depth d) { return {lhs.len_plus_1_ + d, lhs.final_hand_}; }
  /// 詰み手数に整数を加算する
  friend inline MateLen operator+(Depth d, const MateLen& rhs) { return rhs + d; }
  /// 詰み手数から整数を減算する
  friend inline MateLen operator-(const MateLen& lhs, Depth d) { return {lhs.len_plus_1_ - d, lhs.final_hand_}; }

 private:
  /// コンストラクタ。外部から直接構築できないように private に隠す。
  constexpr MateLen(std::uint32_t len_plus_1, std::uint32_t final_hand)
      : len_plus_1_{len_plus_1}, final_hand_{final_hand} {}

  std::uint32_t len_plus_1_;  ///< 詰み手数 + 1。詰み手数に1を足すことで 0手詰めが若干扱いやすくなる。
  std::uint32_t final_hand_;  ///< 詰み局面における駒余り枚数
};

/// 詰み手数の最小値。
constexpr inline MateLen kZeroMateLen = MateLen::Make(0, MateLen::kFinalHandMax);
/// 詰み手数の最大値。
constexpr inline MateLen kMaxMateLen = MateLen::Make(kDepthMax, 0);

/**
 * @brief `MateLen16` or `MateLen` を出力ストリームに出力する。
 * @tparam ML `MateLen16` または `MateLen`
 * @param os  output stream
 * @param mate_len 詰み手数
 * @return output stream
 */
template <typename ML,
          Constraints<std::enable_if_t<std::is_same_v<ML, MateLen16> || std::is_same_v<ML, MateLen>>> = nullptr>
inline std::ostream& operator<<(std::ostream& os, const ML& mate_len) {
  os << mate_len.Len() << "(" << mate_len.FinalHand() << ")";
  return os;
}
}  // namespace komori

#endif  // KOMORI_MATE_LEN_HPP_
