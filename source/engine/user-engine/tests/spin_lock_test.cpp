#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include "../spin_lock.hpp"
#include "tests/test_lib.hpp"

using komori::SpinLock;

TEST(SpinLock, Lock) {
  SpinLock spin_lock;
  Barrier barrier{2};
  std::atomic_int ans = 0;
  std::atomic_int phase = 0;
  const auto result = ParallelExecute(
      std::chrono::milliseconds{100},
      [&]() {
        spin_lock.lock();
        phase++;          // phase 1
        barrier.Await();  // barrier 1
        phase++;          // phase 2
        spin_lock.unlock();
      },
      [&]() {
        barrier.Await();  // barrier 1
        spin_lock.lock();
        ans = phase.load();
        spin_lock.unlock();
      });

  EXPECT_TRUE(result);
  EXPECT_EQ(ans, 2);
}
