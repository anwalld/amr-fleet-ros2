#include <gtest/gtest.h>

#include "robot_console/request_tracker.hpp"

using robot_console::RequestTracker;

TEST(RequestTracker, OnePendingAtATime) {
  RequestTracker t(3.0);
  EXPECT_TRUE(t.canSend());
  t.markSent(7, 100.0);
  EXPECT_FALSE(t.canSend());  // 挂起中不重发
  EXPECT_TRUE(t.markDone(7));
  EXPECT_TRUE(t.canSend());
  EXPECT_EQ(t.timeouts(), 0u);
}

TEST(RequestTracker, TimeoutAfterThreeSeconds) {
  RequestTracker t(3.0);
  t.markSent(1, 10.0);
  EXPECT_FALSE(t.checkTimeout(12.9).has_value());
  EXPECT_FALSE(t.canSend());
  const auto dropped = t.checkTimeout(13.0);
  ASSERT_TRUE(dropped.has_value());
  EXPECT_EQ(*dropped, 1);
  EXPECT_EQ(t.timeouts(), 1u);
  EXPECT_TRUE(t.canSend());  // 下一轮照常发
  EXPECT_FALSE(t.checkTimeout(20.0).has_value());  // 不重复计数
  EXPECT_EQ(t.timeouts(), 1u);
}

TEST(RequestTracker, LateResponseIgnored) {
  RequestTracker t(3.0);
  t.markSent(1, 0.0);
  ASSERT_TRUE(t.checkTimeout(5.0).has_value());
  t.markSent(2, 5.0);
  EXPECT_FALSE(t.markDone(1));  // 迟到的旧应答不影响新请求
  EXPECT_FALSE(t.canSend());
  EXPECT_TRUE(t.markDone(2));
}
