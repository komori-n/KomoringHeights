#ifndef KOMORI_SPIN_LOCK_HPP_
#define KOMORI_SPIN_LOCK_HPP_

#include <atomic>

namespace komori {

/**
 * @brief `std::atomic_flag` を用いたスピンロック
 */
class SpinLock {
 public:
  /**
   * @brief ロックを取得する
   */
  void lock() {
    while (flag_.test_and_set(std::memory_order_acquire)) {
    }
  }

  /**
   * @brief ロックを解放する
   * @pre `lock()` によりロックされている
   */
  void unlock() { flag_.clear(std::memory_order_release); }

 private:
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;  ///< ロックフラグ
};

static_assert(sizeof(SpinLock) <= 1);
}  // namespace komori

#endif  // KOMORI_SPIN_LOCK_HPP_
