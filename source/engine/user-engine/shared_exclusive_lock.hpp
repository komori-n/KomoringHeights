/**
 * @file shared_exclusive_lock.hpp
 */
#ifndef KOMORI_SHARED_EXCLUSIVE_LOCK_HPP_
#define KOMORI_SHARED_EXCLUSIVE_LOCK_HPP_

#include <atomic>

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
    T state = state_.load(std::memory_order_relaxed);
    for (;;) {
      if (state >= 0) {
        if (state_.compare_exchange_weak(state, state + 1, std::memory_order_acquire, std::memory_order_relaxed)) {
          break;
        }
      } else {
        state = state_.load(std::memory_order_relaxed);
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
    for (;;) {
      T state = 0;
      if (state_.compare_exchange_weak(state, -1, std::memory_order_acquire, std::memory_order_relaxed)) {
        break;
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
