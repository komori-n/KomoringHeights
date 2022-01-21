#ifndef USI_HPP_
#define USI_HPP_

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "typedefs.hpp"

namespace komori {

class Score {
 public:
  static Score Unknown(PnDn pn, PnDn dn) {
    // - a log(1/x - 1)
    //   a: Ponanza 定数
    //   x: 勝率(<- dn / (pn + dn))

    constexpr double kA = 600.0;
    dn = std::max(dn, PnDn{1});
    double value = -kA * std::log(static_cast<double>(pn) / static_cast<double>(dn));
    return Score{Kind::kUnknown, static_cast<int>(value)};
  }
  static Score Proven(Depth mate_len, bool is_root_or_node) {
    if (is_root_or_node) {
      return Score{Kind::kWin, mate_len};
    } else {
      return Score{Kind::kLose, mate_len};
    }
  }
  static Score Disproven(Depth mate_len, bool is_root_or_node) {
    if (is_root_or_node) {
      return Score{Kind::kLose, mate_len};
    } else {
      return Score{Kind::kWin, mate_len};
    }
  }

  Score() = default;

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

 private:
  enum class Kind {
    kUnknown,
    kWin,
    kLose,
  };

  static inline constexpr int kMinValue = -32767;
  static inline constexpr int kMaxValue = 32767;

  Score(Kind kind, int value) : kind_{kind}, value_{value} {}

  Kind kind_{Kind::kUnknown};
  int value_{};
};

/**
 * @brief USIプロトコルに従い Info を出力するための構造体
 */
class UsiInfo {
 public:
  enum class KeyKind {
    kDepth,
    kSelDepth,
    kTime,
    kNodes,
    kNps,
    kHashfull,
    kScore,
    kCurrMove,
    kPv,
    kString,
  };

  std::string ToString() const;

  template <typename T,
            std::enable_if_t<std::is_integral_v<std::decay_t<T>> || std::is_floating_point_v<std::decay_t<T>>,
                             std::nullptr_t> = nullptr>
  UsiInfo& Set(KeyKind kind, T&& value) {
    return Set(kind, std::to_string(std::forward<T>(value)));
  }

  UsiInfo& Set(KeyKind kind, Score score) { return Set(kind, score.ToString()); }

  UsiInfo& Set(KeyKind kind, std::string value) {
    if (kind == KeyKind::kPv) {
      pv_ = std::move(value);
    } else if (kind == KeyKind::kString) {
      string_ = std::move(value);
    } else {
      options_[kind] = std::move(value);
      if (kind == KeyKind::kSelDepth && options_.find(KeyKind::kDepth) == options_.end()) {
        options_[KeyKind::kDepth] = "0";
      }
    }
    return *this;
  }

 private:
  std::unordered_map<KeyKind, std::string> options_;

  // PV と String は info の最後に片方だけ出力する必要があるため、他の変数とは持ち方を変える
  std::optional<std::string> pv_;
  std::optional<std::string> string_;
};

std::ostream& operator<<(std::ostream& os, const UsiInfo& usi_output);
}  // namespace komori
#endif  // USI_HPP_
