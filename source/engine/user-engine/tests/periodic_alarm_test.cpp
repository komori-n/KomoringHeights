#include <gtest/gtest.h>

#include <thread>
#include "../periodic_alarm.hpp"

using komori::PeriodicAlarm;

TEST(PeriodicAlarm, Tick) {
  PeriodicAlarm alarm{};

  alarm.Start(100);
  const auto next_tp = std::chrono::system_clock::now() + std::chrono::milliseconds(100);
  while (!alarm.Tick()) {
    // 1ms 寝るとテストが通らないので注意
  }

  const auto diff =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - next_tp).count();
  EXPECT_NEAR(diff, 0, 5);
}
