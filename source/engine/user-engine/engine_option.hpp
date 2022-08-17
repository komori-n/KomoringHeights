#ifndef KOMORI_ENGINE_OPTION_HPP_
#define KOMORI_ENGINE_OPTION_HPP_

#include <string>

#include "../../usi.h"
#include "typedefs.hpp"

namespace komori {

enum class YozumeVerboseLevel {
  kNone = 0,
  kOnlyYozume,
  kYozumeAndUnknown,
  kAll,
  // 範囲判定用
  kEnd,
  kBegin = 0,
};

inline bool operator<(YozumeVerboseLevel lhs, YozumeVerboseLevel rhs) {
  return static_cast<int>(lhs) < static_cast<int>(rhs);
}

struct EngineOption {
  std::uint64_t hash_mb;
  int threads;

  Depth depth_limit;
  std::uint64_t nodes_limit;

  std::uint64_t pv_interval;
  std::uint64_t post_search_count;

  bool root_is_and_node_if_checked;
  YozumeVerboseLevel yozume_print_level;

#if defined(USE_DEEP_DFPN)
  Depth deep_dfpn_d_;
  double deep_dfpn_e_;
#endif

  void Init(USI::OptionsMap& o);
  void Reload(const USI::OptionsMap& o);
};
}  // namespace komori

#endif  // KOMORI_ENGINE_OPTION_HPP_
