#ifndef KOMORI_SHARED_EXCLUSIVE_LOCK_HPP_
#define KOMORI_SHARED_EXCLUSIVE_LOCK_HPP_

#include <atomic>
#include <thread>

namespace komori {
/**
 * @brief std::atomic を用いた Shared-Exclusive lock
 * @tparam T 状態を表す型。符号付き整数型である必要がある。
 */
template <typename T>
class SharedExclusiveLock {
  static_assert(std::is_integral_v<T> && std::is_signed_v<T>);

 public:
  /**
   * @brief 共有ロックを取得する
   */
  void lock_shared() noexcept {
    for (;;) {
      T state = state_.load(std::memory_order_acquire);
      if (state >= 0) {
        if (state_.compare_exchange_weak(state, state + 1, std::memory_order_acquire)) {
          break;
        }
      }
    }
  }

  /**
   * @brief 共有ロックを解放する
   */
  void unlock_shared() noexcept { state_.fetch_sub(1, std::memory_order_release); }

  /**
   * @brief 排他ロックを取得する
   */
  void lock() noexcept {
    T expected = 0;
    if (!state_.compare_exchange_strong(expected, -1, std::memory_order_acquire)) {
      for (;;) {
        T state = state_.load(std::memory_order_acquire);
        if (state == 0) {
          if (state_.compare_exchange_weak(state, -1, std::memory_order_acquire)) {
            break;
          }
        }
      }
    }
  }

  /**
   * @brief 排他ロックを解放する
   */
  void unlock() noexcept { state_.store(0, std::memory_order_release); }

 private:
  /// ロック状態を管理する変数。正なら共有ロック中、負なら排他ロック中、0ならロックされていない。
  std::atomic<T> state_{0};
};
}  // namespace komori

#endif  // KOMORI_SHARED_EXCLUSIVE_LOCK_HPP_
