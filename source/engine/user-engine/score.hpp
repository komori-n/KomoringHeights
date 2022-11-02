/**
 * @file score.hpp
 */
#ifndef KOMORI_SCORE_HPP_
#define KOMORI_SCORE_HPP_

#include "engine_option.hpp"
#include "search_result.hpp"
#include "typedefs.hpp"

namespace komori {
/**
 * @brief 現在の探索状況に基づく評価値っぽいものを計算する。
 */
class Score : DefineNotEqualByEqual<Score> {
  /// 内部で用いる整数型
  using ScoreValue = std::int32_t;

 public:
  /// デフォルトコンストラクタ
  constexpr Score() noexcept = default;

  /**
   * @brief `Score` オブジェクトを構築する。
   * @param method `Score` の計算方法
   * @param result `result` 現在の探索結果
   * @param is_root_or_node 開始局面が OR node かどうか
   * @return `Score` オブジェクト
   *
   * `method` の値に応じて構築方法を変えたいので `static` メソッドとして公開する。
   */
  static Score Make(ScoreCalculationMethod method, const SearchResult& result, bool is_root_or_node) {
    constexpr double kPonanza = 600.0;  // ポナンザ定数

    Score score{};  // 開始局面が AND node なら正負を反転したいので一旦変数に格納する
    if (result.IsFinal()) {
      // 開始局面の手番を基準に評価値を計算しなければならない
      if (result.Pn() == 0) {
        score = Score(Kind::kWin, result.Len().Len());
      } else {
        score = Score(Kind::kLose, result.Len().Len());
      }
    } else {
      switch (method) {
        case ScoreCalculationMethod::kDn:
          score = Score(Kind::kUnknown, result.Dn());
          break;
        case ScoreCalculationMethod::kMinusPn:
          score = Score(Kind::kUnknown, -static_cast<ScoreValue>(result.Pn()));
          break;
        case ScoreCalculationMethod::kPonanza: {
          const double r = static_cast<double>(result.Dn()) / (result.Pn() + result.Dn());
          const double val_real = -kPonanza * std::log((1 - r) / r);
          const ScoreValue val = static_cast<ScoreValue>(val_real);
          score = Score(Kind::kUnknown, val);
        } break;
        default:
          score = Score(Kind::kUnknown, 0);
      }
    }

    return is_root_or_node ? score : -score;
  }

  /// 現在の評価値を USI 文字列で返す
  std::string ToString() const {
    switch (kind_) {
      case Kind::kWin:
        return std::string{"mate "} + std::to_string(value_);
      case Kind::kLose:
        return std::string{"mate -"} + std::to_string(value_);
      default:
        return std::string{"cp "} + std::to_string(value_);
    }
  }

  /// 評価値の正負を反転させる
  Score operator-() const {
    switch (kind_) {
      case Kind::kWin:
        return Score(Kind::kLose, value_);
      case Kind::kLose:
        return Score(Kind::kWin, value_);
      default:
        return Score(Kind::kUnknown, -value_);
    }
  }

  /// `lhs` と `rhs` が等しいかどうか
  friend bool operator==(const Score& lhs, const Score& rhs) noexcept {
    return lhs.kind_ == rhs.kind_ && lhs.value_ == rhs.value_;
  }

 private:
  /// 評価値の種別（勝ちとか負けとか）
  enum class Kind {
    kUnknown,  ///< 詰み／不詰未確定
    kWin,      ///< （開始局面の手番から見て）勝ち
    kLose,     ///< （開始局面の手番から見て）負け
  };

  /// コンストラクタ。`Make()` 以外では構築できないように private に隠しておく
  Score(Kind kind, int value) : kind_{kind}, value_{value} {}

  Kind kind_{Kind::kUnknown};  ///< 評価値諸別
  ScoreValue value_{};         ///< 評価値（kUnknown） or 詰み手数（kWin/kLose）
};
}  // namespace komori

#endif  // KOMORI_SCORE_HPP_
