#include "ttentry.hpp"

namespace komori {
std::ostream& operator<<(std::ostream& os, const UnknownData& data) {
  return os << "UnknownData{pn=" << ToString(data.pn_) << ", dn=" << ToString(data.dn_) << ", hand=" << data.hand_
            << ", min_depth=" << data.min_depth_ << ", secret=" << HexString(data.secret_) << ", parent=("
            << HexString(data.parent_board_key_) << "," << data.parent_hand_ << ")"
            << "}";
}

template <bool kProven>
std::ostream& operator<<(std::ostream& os, const HandsData<kProven>& data) {
  if constexpr (kProven) {
    os << "ProvenData{";
  } else {
    os << "DisprovenData{";
  }
  for (std::size_t i = 0; i < HandsData<kProven>::kHandsLen; ++i) {
    if (i != 0) {
      os << ", ";
    }
    os << data.entries_[i].hand << "/" << data.entries_[i].move << "/" << data.entries_[i].mate_len;
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const RepetitionData& /* data */) {
  return os << "RepetitionData{}";
}

std::ostream& operator<<(std::ostream& os, const SearchResult& search_result) {
  os << search_result.s_amount_.node_state << " " << search_result.s_amount_.amount << " ";
  switch (search_result.GetNodeState()) {
    case NodeState::kProvenState:
      return os << search_result.proven_;
    case NodeState::kDisprovenState:
      return os << search_result.disproven_;
    case NodeState::kRepetitionState:
      return os << search_result.rep_;
    default:
      return os << search_result.unknown_;
  }
}

std::string ToString(const SearchResult& search_result) {
  std::ostringstream os;
  os << search_result;
  return os.str();
}

std::ostream& operator<<(std::ostream& os, const CommonEntry& entry) {
  os << HexString(entry.hash_high_) << " " << static_cast<const SearchResult&>(entry);
  return os;
}

std::string ToString(const CommonEntry& entry) {
  std::ostringstream oss;
  oss << entry;
  return oss.str();
}

template std::ostream& operator<<<false>(std::ostream& os, const HandsData<false>& data);
template std::ostream& operator<<<true>(std::ostream& os, const HandsData<true>& data);
}  // namespace komori
