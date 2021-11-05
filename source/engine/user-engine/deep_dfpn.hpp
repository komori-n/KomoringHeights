#ifndef DEEP_DFPN_HPP_
#define DEEP_DFPN_HPP_

#include "typedefs.hpp"

namespace komori {
#if defined(USE_DEEP_DFPN)
/// deep df-pn のテーブルを初期化する。
void DeepDfpnInit(Depth d, double e);

/// 深さ depth のみ探索ノードの pn, dn の初期値を返す
PnDn InitialPnDn(Depth depth);
#else
inline constexpr PnDn InitialPnDn(Depth /* depth */) {
  return 1;
}
#endif
}  // namespace komori

#endif  // DEEP_DFPN_HPP_