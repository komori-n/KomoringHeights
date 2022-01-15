#include "usi.hpp"

#include <sstream>

namespace komori {
namespace {
constexpr const char* kKeyNames[] = {
    "depth", "seldepth", "time", "nodes", "nps", "hashfull", "score", "currmove", "pv", "string",
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
    if (options_.find(KeyKind::kCurrMove) == options_.end()) {
      auto space_pos = pv_->find_first_of(' ');
      oss << " " << KeyName(KeyKind::kCurrMove) << " " << pv_->substr(0, space_pos);
    }

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