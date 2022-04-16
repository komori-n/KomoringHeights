#include "engine_option.hpp"

#include <cmath>
#include <limits>

namespace komori {
namespace {
template <typename T>
T ReadValue(const USI::OptionsMap& o, const std::string& key);

template <typename N>
N ReadValue(const USI::OptionsMap& o, const std::string& key) {
  static_assert(std::is_integral_v<N>);

  if (auto itr = o.find(key); itr != o.end()) {
    if (auto val = static_cast<N>(itr->second)) {
      return val;
    }
  }

  return std::numeric_limits<N>::max();
}

template <>
bool ReadValue<bool>(const USI::OptionsMap& o, const std::string& key) {
  if (auto itr = o.find(key); itr != o.end()) {
    return static_cast<bool>(itr->second);
  }
  return false;
}
}  // namespace

void EngineOption::Init(USI::OptionsMap& o) {
  o["DepthLimit"] << USI::Option(0, 0, kMaxNumMateMoves);
  o["NodesLimit"] << USI::Option(0, 0, INT64_MAX);
  o["PvInterval"] << USI::Option(1000, 0, 1000000);

  o["PostSearchCount"] << USI::Option(400, 0, INT64_MAX);

  o["RootIsAndNodeIfChecked"] << USI::Option(true);
  o["YozumePrintLevel"] << USI::Option(2, 0, 3);

#if defined(USE_DEEP_DFPN)
  o["DeepDfpnPerMile"] << USI::Option(5, 0, 10000);
  o["DeepDfpnMaxVal"] << USI::Option(1000000, 1, INT64_MAX);
#endif  // defined(USE_DEEP_DFPN)
}

void EngineOption::Reload(const USI::OptionsMap& o) {
  hash_mb = ReadValue<std::uint64_t>(o, "USI_Hash");
  threads = ReadValue<int>(o, "Threads");

  depth_limit = ReadValue<Depth>(o, "DepthLimit");
  if (depth_limit > kMaxNumMateMoves) {
    depth_limit = kMaxNumMateMoves;
  }
  nodes_limit = ReadValue<std::uint64_t>(o, "NodesLimit");

  pv_interval = ReadValue<std::uint64_t>(o, "PvInterval");
  if (pv_interval == std::numeric_limits<std::uint64_t>::max()) {
    pv_interval = 0;
  }
  post_search_count = ReadValue<std::uint64_t>(o, "PostSearchCount");
  if (post_search_count == std::numeric_limits<std::uint64_t>::max()) {
    post_search_count = 0;
  }

  root_is_and_node_if_checked = ReadValue<bool>(o, "RootIsAndNodeIfChecked");

  auto yozume_level_int = ReadValue<int>(o, "YozumePrintLevel");
  if (static_cast<int>(YozumeVerboseLevel::kBegin) <= yozume_level_int &&
      yozume_level_int < static_cast<int>(YozumeVerboseLevel::kEnd)) {
    yozume_print_level = static_cast<YozumeVerboseLevel>(yozume_level_int);
  } else {
    yozume_print_level = YozumeVerboseLevel::kNone;
  }

#if defined(USE_DEEP_DFPN)
  if (auto val = ReadValue<int>(o, "DeepDfpnPerMile"); val != std::numeric_limits<int>::max()) {
    deep_dfpn_e_ = 0.001 * val + 1.0;
    auto max = ReadValue<int>(o, "DeepDfpnMaxVal");
    deep_dfpn_d_ = static_cast<Depth>(std::log(static_cast<double>(max)) / std::log(e));
  } else {
    deep_dfpn_d_ = 0;
    deep_dfpn_e_ = 1.0;
  }
  deep_dfpn_d_ = ReadValue<double>(o, "DeepDfpnMaxVal");
#endif  // if defined(USE_DEEP_DFPN)
}
}  // namespace komori
