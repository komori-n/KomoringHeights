#ifndef KOMORI_TTV3_HPP_
#define KOMORI_TTV3_HPP_

#include <cstdint>

#include "mate_len.hpp"
#include "typedefs.hpp"

namespace komori {
namespace ttv3 {
/// 探索量。TTでエントリを消す際の判断に用いる。
using SearchAmount = std::uint32_t;

class alignas(64) Entry {
 public:
  constexpr void SetNull() noexcept { hand_ = kNullHand; }
  constexpr bool IsNull() noexcept { return hand_ == kNullHand; }

 private:
  Hand hand_;            ///< 現局面の持ち駒
  SearchAmount amount_;  ///< 現局面の探索量
  Key board_key_;        ///< 盤面ハッシュ値

  struct {
    MateLen16 len;     ///< 詰み手数
    Move16 best_move;  ///< 最善手
  } proven_;           ///< 詰み情報

  struct {
    MateLen16 len;     ///< 不詰手数
    Move16 best_move;  ///< 最善手
  } disproven_;        ///< 不詰情報

  PnDn pn_;  ///< pn値
  PnDn dn_;  ///< dn値

  Key parent_key_;              ///< 親局面のハッシュ値
  Hand parent_hand_;            ///< 親局面の持ち駒
  std::int16_t min_depth_;      ///< 探索深さ
  std::int8_t may_repetition_;  ///< 現局面が千日手の可能性があるか

  std::uint64_t secret_;  ///< 秘密の値
};

static_assert(sizeof(Entry) <= 64);
static_assert(alignof(Entry) == 64);
}  // namespace ttv3
}  // namespace komori

#endif  // KOMORI_TTV3_HPP_
