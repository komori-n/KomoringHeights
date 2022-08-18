#ifndef KOMORI_USI_HPP_
#define KOMORI_USI_HPP_

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "typedefs.hpp"

namespace komori {
/**
 * @brief USIプロトコルに従い Info を出力するための構造体
 */
class UsiInfo {
 public:
  UsiInfo() = default;
  UsiInfo(const UsiInfo&) = default;
  UsiInfo(UsiInfo&&) noexcept = default;
  UsiInfo& operator=(const UsiInfo&) = default;
  UsiInfo& operator=(UsiInfo&&) noexcept = default;
  ~UsiInfo() = default;

  enum class KeyKind {
    kDepth,
    kSelDepth,
    kTime,
    kNodes,
    kNps,
    kHashfull,
    kCurrMove,
    kPv,
    kString,
  };

  std::string ToString() const;

  template <typename T,
            Constraints<std::enable_if_t<std::is_integral_v<std::decay_t<T>> ||
                                         std::is_floating_point_v<std::decay_t<T>>>> = nullptr>
  UsiInfo& Set(KeyKind kind, T&& value) {
    return Set(kind, std::to_string(std::forward<T>(value)));
  }

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

  /// 探索情報をマージする。同じ key に対し両方に value がある場合は、左辺値の値を採用する。
  UsiInfo& operator|=(const UsiInfo& rhs) {
    for (const auto& [key, value] : rhs.options_) {
      if (options_.find(key) == options_.end()) {
        options_[key] = value;
      }
    }
    if (pv_ == std::nullopt && string_ == std::nullopt) {
      pv_ = std::move(rhs.pv_);
      string_ = std::move(rhs.string_);
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
UsiInfo operator|(const UsiInfo& lhs, const UsiInfo& rhs);
}  // namespace komori
#endif  // KOMORI_USI_HPP_
