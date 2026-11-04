#include "roo_transport/link/internal/thread_safe/outgoing_data_ready_notification.h"

#include "gtest/gtest.h"
#include "roo_threads/atomic.h"
#include "roo_threads/thread.h"

namespace roo_transport {
namespace internal {
namespace {

// Verifies notification-before-wait and that repeated notifications are
// coalesced into one wakeup, without leaving a stale wakeup for the next wait.
TEST(OutgoingDataReadyNotificationTest, CoalescesPendingNotifications) {
  OutgoingDataReadyNotification notification;
  notification.notify();
  notification.notify();
  EXPECT_TRUE(notification.await(10000));
  EXPECT_FALSE(notification.await(0));
  notification.notify();
  EXPECT_TRUE(notification.await(0));
}

// Verifies that notifications racing the waiter's predicate check and sleep
// registration are preserved across consecutive handoffs.
TEST(OutgoingDataReadyNotificationTest, RepeatedConcurrentHandoffs) {
  OutgoingDataReadyNotification notification;
  roo::atomic<int> received{0};
  roo::thread waiter([&] {
    for (int i = 0; i < 100; ++i) {
      if (!notification.await(100000)) return;
      ++received;
    }
  });
  const roo_time::Uptime deadline =
      roo_time::Uptime::Now() + roo_time::Seconds(5);
  for (int i = 0; i < 100; ++i) {
    notification.notify();
    while (received.load() <= i && roo_time::Uptime::Now() < deadline) {
      roo::this_thread::yield();
    }
    if (received.load() <= i) break;
  }
  waiter.join();
  EXPECT_EQ(received.load(), 100);
}

}  // namespace
}  // namespace internal
}  // namespace roo_transport
