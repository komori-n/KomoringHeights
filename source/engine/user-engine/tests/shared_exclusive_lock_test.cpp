#include <gtest/gtest.h>

#include <atomic>

#include "../shared_exclusive_lock.hpp"
#include "test_lib.hpp"

using komori::SharedExclusiveLock;

TEST(SharedExclusiveLock, SharedLockBlocksExclusiveLock) {
  SharedExclusiveLock<std::int8_t> lock;
  Barrier barrier{2};
  std::atomic<int> ans = 0;
  std::atomic<int> phase = 0;

  const auto result = ParallelExecute(
      std::chrono::milliseconds{100},
      [&]() {
        lock.lock_shared();
        barrier.Await();  // barrier 1
        phase++;          // phase 1

        lock.unlock_shared();
        phase++;  // phase 2
      },
      [&]() {
        barrier.Await();  // barrier 1
        lock.lock();
        ans = phase.load();
        lock.unlock();
      });

  EXPECT_TRUE(result);
  // phase == 1 以降でロックが取れるはず
  EXPECT_GE(ans.load(), 1);
}

TEST(SharedExclusiveLock, SharedLockWhileSharedLock) {
  SharedExclusiveLock<std::int8_t> lock;
  Barrier barrier{2};

  const auto result = ParallelExecute(
      std::chrono::milliseconds{100},
      [&]() {
        lock.lock_shared();
        barrier.Await();  // barrier 1
        barrier.Await();  // barrier 2
        lock.unlock_shared();
      },
      [&]() {
        barrier.Await();  // barrier 1
        lock.lock_shared();
        lock.unlock_shared();
        barrier.Await();  // barrier 2
      });
  EXPECT_TRUE(result);
}

TEST(SharedExclusiveLock, ExclusiveLockBlocksSharedLock) {
  SharedExclusiveLock<std::int8_t> lock;
  Barrier barrier{2};
  std::atomic<int> ans = 0;
  std::atomic<int> phase = 0;

  const auto result = ParallelExecute(
      std::chrono::milliseconds{100},
      [&]() {
        lock.lock();
        phase++;          // phase 1
        barrier.Await();  // barrier 1
        phase++;          // phase 2
        lock.unlock();
      },
      [&]() {
        barrier.Await();  // barrier 1
        lock.lock_shared();
        ans = phase.load();
        lock.unlock_shared();
      });
  EXPECT_TRUE(result);
  EXPECT_EQ(ans, 2);
}

TEST(SharedExclusiveLock, ExclusiveLockBlocksExclusiveLock) {
  SharedExclusiveLock<std::int8_t> lock;
  Barrier barrier{2};
  std::atomic<int> ans = 0;
  std::atomic<int> phase = 0;

  const auto result = ParallelExecute(
      std::chrono::milliseconds{100},
      [&]() {
        lock.lock();
        phase++;          // phase 1
        barrier.Await();  // barrier 1
        phase++;          // phase 2
        lock.unlock();
      },
      [&]() {
        barrier.Await();  // barrier 1
        lock.lock();
        ans = phase.load();
        lock.unlock();
      });
  EXPECT_TRUE(result);
  EXPECT_EQ(ans, 2);
}
