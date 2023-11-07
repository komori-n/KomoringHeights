#ifndef KOMORI_THREAD_INITIALIZATION_HPP_
#define KOMORI_THREAD_INITIALIZATION_HPP_

#include "initial_estimation.hpp"
#include "ttquery.hpp"
#include "typedefs.hpp"

namespace komori {
inline void InitializeThread(std::uint32_t id) {
  tl_thread_id = id;
  InitBriefEvaluation(id);
  tt::InitializeTTNoise(id);
}
}  // namespace komori

#endif  // KOMORI_THREAD_INITIALIZATION_HPP_
