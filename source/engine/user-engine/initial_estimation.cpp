#include "initial_estimation.hpp"

#include <cmath>
#include <vector>

#if defined(USE_DEEP_DFPN)
namespace {
Depth g_d;
std::vector<komori::PnDn> g_pndn_tbl;
}  // namespace

namespace komori {
void DeepDfpnInit(Depth d, double e) {
  g_d = d;

  g_pndn_tbl.clear();
  g_pndn_tbl.reserve(g_d);
  for (Depth di = 0; di < d; ++di) {
    PnDn val = static_cast<PnDn>(std::pow(e, d - di));
    g_pndn_tbl.push_back(val);
  }
}

PnDn InitialDeepPnDn(Depth depth) {
  if (depth < g_d) {
    return g_pndn_tbl[depth];
  } else {
    return 1;
  }
}
}  // namespace komori
#endif  // defined(USE_DEEP_DFPN)
