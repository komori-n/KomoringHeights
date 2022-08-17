#ifndef KOMORI_MATE_LEN_HPP_
#define KOMORI_MATE_LEN_HPP_

#include "typedefs.hpp"

namespace komori {
/// 詰み／不詰手数とその手順における駒余り枚数を16bitでまとめた構造体。
struct MateLen16 : DefineNotEqualByEqual<MateLen16>, DefineComparisonOperatorsByEqualAndLess<MateLen16> {
  std::uint16_t len_plus_1 : 12;
  std::uint16_t final_hand : 4;

  template <typename T>
  constexpr MateLen16(const T& len_plus_1, const typename Identity<T>::type& final_hand)
      : len_plus_1{static_cast<std::uint16_t>(len_plus_1)},
        final_hand{std::min(static_cast<std::uint16_t>(final_hand), std::uint16_t{15})} {}
  MateLen16() = default;

  constexpr friend bool operator==(const MateLen16& lhs, const MateLen16& rhs) noexcept {
    return lhs.len_plus_1 == rhs.len_plus_1 && lhs.final_hand == rhs.final_hand;
  }

  constexpr friend bool operator<(const MateLen16& lhs, const MateLen16& rhs) noexcept {
    if (lhs.len_plus_1 != rhs.len_plus_1) {
      return lhs.len_plus_1 < rhs.len_plus_1;
    }

    return lhs.final_hand > rhs.final_hand;
  }

  friend inline MateLen16 operator+(const MateLen16& lhs, Depth d) {
    return MateLen16{static_cast<std::uint16_t>(lhs.len_plus_1 + d), lhs.final_hand};
  }

  friend inline MateLen16 operator+(Depth d, const MateLen16& rhs) { return rhs + d; }

  friend inline MateLen16 operator-(const MateLen16& lhs, Depth d) {
    return MateLen16{static_cast<std::uint16_t>(lhs.len_plus_1 - d), lhs.final_hand};
  }
};

constexpr inline MateLen16 kZeroMateLen16{1, 15};
constexpr inline MateLen16 kMaxMateLen16{kMaxNumMateMoves + 1, 0};

struct MateLen : DefineNotEqualByEqual<MateLen>, DefineComparisonOperatorsByEqualAndLess<MateLen> {
  static constexpr inline std::uint32_t kFinalHandMax = 38;

  std::uint32_t len_plus_1;
  std::uint32_t final_hand;

  constexpr MateLen(std::uint32_t len_plus_1, std::uint32_t final_hand)
      : len_plus_1{len_plus_1}, final_hand{final_hand} {}
  constexpr explicit MateLen(const MateLen16& mate_len16)
      : len_plus_1{mate_len16.len_plus_1}, final_hand{mate_len16.final_hand} {}
  MateLen() = default;

  constexpr MateLen16 To16() const {
    const auto len_16 = static_cast<std::uint16_t>(len_plus_1);
    const auto final_16 = static_cast<std::uint16_t>(final_hand);

    return {len_16, std::min(final_16, std::uint16_t{15})};
  }

  constexpr friend bool operator==(const MateLen& lhs, const MateLen& rhs) {
    return lhs.len_plus_1 == rhs.len_plus_1 && lhs.final_hand == rhs.final_hand;
  }

  constexpr friend bool operator<(const MateLen& lhs, const MateLen& rhs) {
    if (lhs.len_plus_1 != rhs.len_plus_1) {
      return lhs.len_plus_1 < rhs.len_plus_1;
    }

    return lhs.final_hand > rhs.final_hand;
  }

  friend inline MateLen operator+(const MateLen& lhs, Depth d) { return {lhs.len_plus_1 + d, lhs.final_hand}; }
  friend inline MateLen operator+(Depth d, const MateLen& rhs) { return rhs + d; }
  friend inline MateLen operator-(const MateLen& lhs, Depth d) { return {lhs.len_plus_1 - d, lhs.final_hand}; }
};

constexpr inline MateLen kZeroMateLen{1, MateLen::kFinalHandMax};
constexpr inline MateLen kMaxMateLen{kMaxNumMateMoves + 1, 0};

inline MateLen Succ(const MateLen& len) {
  if (len.final_hand == 0) {
    return {len.len_plus_1 + 1, MateLen::kFinalHandMax};
  } else {
    return {len.len_plus_1, len.final_hand - 1};
  }
}

inline MateLen Succ2(const MateLen& len) {
  if (len.final_hand == 0) {
    return {len.len_plus_1 + 2, MateLen::kFinalHandMax};
  } else {
    return {len.len_plus_1, len.final_hand - 1};
  }
}

inline MateLen Prec(const MateLen& len) {
  if (len.final_hand == MateLen::kFinalHandMax) {
    return {len.len_plus_1 - 1, 0};
  } else {
    return {len.len_plus_1, len.final_hand + 1};
  }
}

inline MateLen Prec2(const MateLen& len) {
  if (len.final_hand == MateLen::kFinalHandMax) {
    return {len.len_plus_1 - 2, 0};
  } else {
    return {len.len_plus_1, len.final_hand + 1};
  }
}

inline std::ostream& operator<<(std::ostream& os, const MateLen16& mate_len) {
  os << static_cast<int>(mate_len.len_plus_1) - 1 << "(" << mate_len.final_hand << ")";
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const MateLen& mate_len) {
  os << static_cast<int>(mate_len.len_plus_1) - 1 << "(" << mate_len.final_hand << ")";
  return os;
}
}  // namespace komori

#endif  // KOMORI_MATE_LEN_HPP_
