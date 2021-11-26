#ifndef USI_HPP_
#define USI_HPP_
#include <optional>
#include <type_traits>

#include "typedefs.hpp"

namespace komori {

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
    kPv,
    kString,
  };

  static UsiInfo String(const std::string& str) {
    UsiInfo usi_output{};
    usi_output.Set(KeyKind::kString, std::move(str));
    return usi_output;
  }

  std::string ToString() const;

  template <typename T,
            std::enable_if_t<std::is_integral_v<std::decay_t<T>> || std::is_floating_point_v<std::decay_t<T>>,
                             std::nullptr_t> = nullptr>
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