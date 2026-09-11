#include "roo_transport/rpc/client.h"
#include "roo_transport/rpc/server.h"
#include "roo_transport/rpc/internal/header.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include "roo_threads/thread.h"
#include "roo_threads/condition_variable.h"
#include <vector>

#include "gtest/gtest.h"

namespace roo_transport {
namespace {
class TestMessaging : public Messaging {
 public:
  using Messaging::received;
  using Messaging::reset;
  std::function<bool()> on_send = [] { return false; };
  bool send(const roo::byte*, size_t, const roo::byte*, size_t,
            ConnectionId* id) override {
    if (id) *id = 1;
    return on_send();
  }
  bool sendContinuation(ConnectionId, const roo::byte*, size_t,
                        const roo::byte*, size_t) override { return false; }
};

TEST(RpcClient, FailedSendsReleaseCallbacksBeforeReturning) {
  TestMessaging messaging;
  RpcClient client(messaging);
  client.begin();
  int callbacks = 0;
  auto capture = std::make_shared<int>(0);
  auto cb = [&, capture](const roo::byte*, size_t, RpcStatus) { ++callbacks; };
  EXPECT_EQ(kUnavailable, client.sendUnaryRpc(1, nullptr, 0, cb));
  EXPECT_EQ(kUnavailable,
            client.sendUnaryRpcWithTimeout(1, nullptr, 0, 10, cb));
  EXPECT_EQ(2, capture.use_count());
  messaging.reset(1);
  EXPECT_EQ(0, callbacks);
  UnaryStub<uint32_t, uint32_t> stub(client, 1);
  uint32_t response = 0;
  EXPECT_EQ(kUnavailable, stub.call(7, response));
  messaging.reset(1);  // Must not invoke the stub's expired stack captures.
}

TEST(RpcClient, FailedSendWaitsForCallbackClaimedByReset) {
  TestMessaging messaging;
  RpcClient client(messaging);
  client.begin();
  roo::mutex mutex;
  roo::condition_variable cv;
  bool entered = false;
  bool release = false;
  std::atomic<bool> returned{false};
  std::atomic<bool> failing_send{false};
  roo::thread resetter;
  messaging.on_send = [&] {
    resetter = roo::thread([&] { messaging.reset(1); });
    roo::unique_lock<roo::mutex> lock(mutex);
    cv.wait(lock, [&] { return entered; });
    failing_send = true;
    return false;
  };
  roo::thread sender([&] {
    EXPECT_EQ(kUnavailable, client.sendUnaryRpc(1, nullptr, 0,
        [&](const roo::byte*, size_t, RpcStatus status) {
          EXPECT_EQ(kUnavailable, status);
          roo::unique_lock<roo::mutex> lock(mutex);
          entered = true;
          cv.notify_all();
          cv.wait(lock, [&] { return release; });
        }));
    returned = true;
  });
  while (!failing_send) roo::this_thread::yield();
  roo::this_thread::sleep_for(roo_time::Millis(20));
  EXPECT_FALSE(returned);
  {
    roo::lock_guard<roo::mutex> lock(mutex);
    release = true;
  }
  cv.notify_all();
  sender.join();
  resetter.join();
  EXPECT_TRUE(returned);
}
}  // namespace
}  // namespace roo_transport
