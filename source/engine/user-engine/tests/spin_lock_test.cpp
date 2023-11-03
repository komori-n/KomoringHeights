#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include "../spin_lock.hpp"

using komori::SpinLock;

TEST(SpinLock, Lock) {
  std::atomic_int value = 0;
  SpinLock spin_lock;
  std::thread th([&value, &spin_lock]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    spin_lock.lock();
    value = 334;
    spin_lock.unlock();
  });

  spin_lock.lock();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(value, 0);
  spin_lock.unlock();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(value, 334);

  th.join();
}
