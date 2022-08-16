#ifndef KOMORI_MATE_LEN_HPP_
#define KOMORI_MATE_LEN_HPP_

#include "typedefs.hpp"

namespace komori {
/// 詰み／不詰手数とその手順における駒余り枚数を16bitでまとめた構造体。
struct MateLen : DefineNotEqualByEqual<MateLen>, DefineComparisonOperatorsByEqualAndLess<MateLen> {
  std::uint16_t len_plus_1 : 12;
  std::uint16_t final_hand : 4;

  template <typename T>
  constexpr MateLen(const T& len_plus_1, const typename Identity<T>::type& final_hand)
      : len_plus_1{static_cast<std::uint16_t>(len_plus_1)},
        final_hand{std::min(static_cast<std::uint16_t>(final_hand), std::uint16_t{15})} {}
  MateLen() = default;

  constexpr friend bool operator==(const MateLen& lhs, const MateLen& rhs) noexcept {
    return lhs.len_plus_1 == rhs.len_plus_1 && lhs.final_hand == rhs.final_hand;
  }

  constexpr friend bool operator<(const MateLen& lhs, const MateLen& rhs) noexcept {
    if (lhs.len_plus_1 != rhs.len_plus_1) {
      return lhs.len_plus_1 < rhs.len_plus_1;
    }

    return lhs.final_hand > rhs.final_hand;
  }
};

constexpr inline MateLen kZeroMateLen{1, 15};
constexpr inline MateLen kMaxMateLen{kMaxNumMateMoves + 1, 0};

inline MateLen operator+(const MateLen& lhs, Depth d) {
  return MateLen{static_cast<std::uint16_t>(lhs.len_plus_1 + d), lhs.final_hand};
}

inline MateLen operator+(Depth d, const MateLen& rhs) {
  return rhs + d;
}

inline MateLen operator-(const MateLen& lhs, Depth d) {
  return MateLen{static_cast<std::uint16_t>(lhs.len_plus_1 - d), lhs.final_hand};
}

inline MateLen Succ(const MateLen& len) {
  if (len.final_hand == 0) {
    return {static_cast<std::uint16_t>(len.len_plus_1 + 1), 15};
  } else {
    return {len.len_plus_1, static_cast<std::uint16_t>(len.final_hand - 1)};
  }
}

inline MateLen Succ2(const MateLen& len) {
  if (len.final_hand == 0) {
    return {static_cast<std::uint16_t>(len.len_plus_1 + 2), 15};
  } else {
    return {len.len_plus_1, static_cast<std::uint16_t>(len.final_hand - 1)};
  }
}

inline MateLen Prec(const MateLen& len) {
  if (len.final_hand == 15) {
    return {static_cast<std::uint16_t>(len.len_plus_1 - 1), 0};
  } else {
    return {len.len_plus_1, static_cast<std::uint16_t>(len.final_hand + 1)};
  }
}

inline MateLen Prec2(const MateLen& len) {
  if (len.final_hand == 15) {
    return {static_cast<std::uint16_t>(len.len_plus_1 - 2), 0};
  } else {
    return {len.len_plus_1, static_cast<std::uint16_t>(len.final_hand + 1)};
  }
}

inline std::ostream& operator<<(std::ostream& os, const MateLen& mate_len) {
  os << static_cast<int>(mate_len.len_plus_1) - 1 << "(" << mate_len.final_hand << ")";
  return os;
}
}  // namespace komori

#endif  // KOMORI_MATE_LEN_HPP_