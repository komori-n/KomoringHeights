#ifndef KOMORI_THREAD_INITIALIZATION_HPP_
#define KOMORI_THREAD_INITIALIZATION_HPP_

#include "initial_estimation.hpp"
#include "ttquery.hpp"
#include "typedefs.hpp"

namespace komori {
/**
 * @brief thread local 変数を初期化する
 * @param id          自身の thread id
 * @param num_threads スレッド数
 *
 * thread local 変数たちを初期化する。探索開始前に呼び出すこと。
 */
inline void InitializeThread(std::uint32_t id, std::uint32_t num_threads) {
  tl_thread_id = id;
  tl_gc_thread = (id == num_threads - 1);
  InitBriefEvaluation(id);
  tt::InitializeTTNoise(id);
}
}  // namespace komori

#endif  // KOMORI_THREAD_INITIALIZATION_HPP_
