#include <gtest/gtest.h>

#include <thread>

#include "../shared_exclusive_lock.hpp"

using komori::SharedExclusiveLock;

TEST(SharedExclusiveLock, SharedLock) {
  SharedExclusiveLock<std::int8_t> lock;
  std::atomic<int> ans = 0;
  std::atomic<int> phase = 0;

  std::thread thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    lock.lock();
    ans = phase.load();
    lock.unlock();
  });

  phase = 1;
  lock.lock_shared();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  phase = 2;
  lock.lock_shared();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  phase = 3;
  lock.lock_shared();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  phase = 4;
  lock.unlock_shared();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  phase = 5;
  lock.lock_shared();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  phase = 6;
  lock.unlock_shared();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  phase = 7;
  lock.unlock_shared();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  phase = 8;
  lock.unlock_shared();

  thread.join();
  EXPECT_EQ(ans, 8);
}
