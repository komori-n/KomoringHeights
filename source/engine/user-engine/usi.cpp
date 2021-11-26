#include "usi.hpp"

#include <sstream>

namespace komori {
namespace {
constexpr const char* kKeyNames[] = {
    "depth", "seldepth", "time", "nodes", "nps", "hashfull", "score", "pv", "string",
};

auto KeyName(UsiInfo::KeyKind kind) {
  return kKeyNames[static_cast<int>(kind)];
}
}  // namespace

std::string UsiInfo::ToString() const {
  std::ostringstream oss;

  oss << "info";
  for (const auto& [key, value] : options_) {
    oss << " " << KeyName(key) << " " << value;
  }

  if (pv_) {
    oss << " " << KeyName(KeyKind::kPv) << " " << *pv_;
  } else if (string_) {
    oss << " " << KeyName(KeyKind::kString) << " " << *string_;
  }
  return oss.str();
}

std::ostream& operator<<(std::ostream& os, const UsiInfo& usi_output) {
  return os << usi_output.ToString();
}
}  // namespace komori