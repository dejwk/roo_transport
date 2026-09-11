#include <atomic>
#include <vector>

#include "gtest/gtest.h"
#include "roo_threads/condition_variable.h"
#include "roo_threads/thread.h"
#include "roo_transport/rpc/client.h"
#include "roo_transport/rpc/internal/header.h"
#include "roo_transport/rpc/server.h"

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
  struct Response {
    ConnectionId connection_id;
    RpcHeader header;
  };
  bool sendContinuation(ConnectionId id, const roo::byte* header, size_t size,
                        const roo::byte*, size_t) override {
    RpcHeader parsed;
    EXPECT_GT(parsed.deserialize(header, size), 0u);
    roo::lock_guard<roo::mutex> lock(mutex);
    responses.push_back({id, parsed});
    changed.notify_all();
    return true;
  }
  std::vector<Response> awaitResponses(size_t count) {
    roo::unique_lock<roo::mutex> lock(mutex);
    changed.wait_until(lock, roo_time::Uptime::Now() + roo_time::Seconds(2),
                       [&] { return responses.size() >= count; });
    return responses;
  }
  void request(ConnectionId id, const RpcHeader& header) {
    roo::byte bytes[RpcHeader::kMaxSerializedSize];
    size_t size = header.serialize(bytes, sizeof(bytes));
    received(id, bytes, size);
  }

 private:
  roo::mutex mutex;
  roo::condition_variable changed;
  std::vector<Response> responses;
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
    EXPECT_EQ(
        kUnavailable,
        client.sendUnaryRpc(1, nullptr, 0,
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
TEST(RpcServer, UnknownFunctionReturnsUnimplementedAndReleasesRequest) {
  TestMessaging messaging;
  FunctionTable functions;
  RpcServer server(messaging, &functions);
  server.begin();
  messaging.request(1, RpcHeader::NewUnaryRequest(99, 7));
  auto responses = messaging.awaitResponses(1);
  ASSERT_EQ(1u, responses.size());
  EXPECT_EQ(1u, responses[0].connection_id);
  EXPECT_EQ(7u, responses[0].header.streamId());
  EXPECT_EQ(kUnimplemented, responses[0].header.responseStatus());
  // Reusing the ID must not be rejected as a pending duplicate.
  messaging.request(1, RpcHeader::NewUnaryRequest(99, 7));
  EXPECT_EQ(2u, messaging.awaitResponses(2).size());
}
TEST(RpcServer, OldHandlerCannotEraseReusedStreamOnNewConnection) {
  TestMessaging messaging;
  std::vector<RequestHandle> handles;
  FunctionTable functions = {
      {1, [&](RequestHandle handle, const roo::byte*, size_t, bool) {
         handles.push_back(handle);
       }}};
  RpcServer server(messaging, &functions);
  server.begin();
  messaging.request(1, RpcHeader::NewUnaryRequest(1, 7));
  messaging.request(2, RpcHeader::NewUnaryRequest(1, 7));
  ASSERT_EQ(2u, handles.size());
  handles[0].sendSuccessResponse(nullptr, 0, true);
  handles[0].sendFailureResponse(kUnknown, "late failure");
  handles[1].sendSuccessResponse(nullptr, 0, true);
  auto responses = messaging.awaitResponses(1);
  ASSERT_EQ(1u, responses.size());
  EXPECT_EQ(2u, responses[0].connection_id);
  EXPECT_EQ(7u, responses[0].header.streamId());
  EXPECT_EQ(kOk, responses[0].header.responseStatus());
}
TEST(RpcHeader, TimeoutRoundTripsIncludingZero) {
  for (uint32_t timeout : {0u, 1u, 1000u, UINT32_MAX}) {
    roo::byte bytes[RpcHeader::kMaxSerializedSize];
    auto header = RpcHeader::NewUnaryRequest(123, 7, timeout);
    size_t size = header.serialize(bytes, sizeof(bytes));
    RpcHeader parsed;
    ASSERT_EQ(size, parsed.deserialize(bytes, size));
    ASSERT_TRUE(parsed.hasTimeout());
    EXPECT_EQ(timeout, parsed.timeoutMs());
    EXPECT_EQ(123u, parsed.functionId());
  }
  EXPECT_FALSE(RpcHeader::NewUnaryRequest(123, 7).hasTimeout());
}

TEST(RpcServer, UnansweredRequestExpiresWithoutFurtherTraffic) {
  TestMessaging messaging;
  std::vector<RequestHandle> handles;
  FunctionTable functions = {{1, [&](RequestHandle h, const roo::byte*, size_t,
                                     bool) { handles.push_back(h); }}};
  RpcServer server(messaging, &functions);
  server.begin();
  messaging.request(1, RpcHeader::NewUnaryRequest(1, 7, 20));
  auto responses = messaging.awaitResponses(1);
  ASSERT_EQ(1u, responses.size());
  EXPECT_EQ(7u, responses[0].header.streamId());
  EXPECT_EQ(kDeadlineExceeded, responses[0].header.responseStatus());
  ASSERT_EQ(1u, handles.size());
  handles[0].sendSuccessResponse(nullptr, 0, true);
  handles[0].sendFailureResponse(kUnknown, "too late");
  server.end();
  EXPECT_EQ(1u, messaging.awaitResponses(1).size());
}

TEST(RpcServer, EarlierDeadlineWakesTimerAndCompletedRequestsDoNotExpire) {
  TestMessaging messaging;
  std::vector<RequestHandle> handles;
  FunctionTable functions = {{1, [&](RequestHandle h, const roo::byte*, size_t,
                                     bool) { handles.push_back(h); }}};
  RpcServer server(messaging, &functions);
  server.begin();
  messaging.request(1, RpcHeader::NewUnaryRequest(1, 1, 60000));
  handles[0].sendSuccessResponse(nullptr, 0, true);
  messaging.request(1, RpcHeader::NewUnaryRequest(1, 2, 60000));
  messaging.request(1, RpcHeader::NewUnaryRequest(1, 3, 20));
  auto responses = messaging.awaitResponses(2);
  ASSERT_EQ(2u, responses.size());
  EXPECT_EQ(1u, responses[0].header.streamId());
  EXPECT_EQ(kOk, responses[0].header.responseStatus());
  EXPECT_EQ(3u, responses[1].header.streamId());
  EXPECT_EQ(kDeadlineExceeded, responses[1].header.responseStatus());
  // Shutdown must wake the timer even though the next deadline is a minute
  // away.
  server.end();
  EXPECT_EQ(2u, messaging.awaitResponses(2).size());
}

TEST(RpcServer, ZeroTimeoutSkipsHandlerAndResetDiscardsTimers) {
  TestMessaging messaging;
  int invoked = 0;
  FunctionTable functions = {
      {1, [&](RequestHandle, const roo::byte*, size_t, bool) { ++invoked; }}};
  RpcServer server(messaging, &functions);
  server.begin();
  messaging.request(1, RpcHeader::NewUnaryRequest(1, 1, 0));
  auto responses = messaging.awaitResponses(1);
  ASSERT_EQ(1u, responses.size());
  EXPECT_EQ(kDeadlineExceeded, responses[0].header.responseStatus());
  EXPECT_EQ(0, invoked);
  messaging.request(1, RpcHeader::NewUnaryRequest(1, 2, 40));
  messaging.reset(1);
  messaging.request(2, RpcHeader::NewUnaryRequest(1, 2, 80));
  responses = messaging.awaitResponses(2);
  ASSERT_EQ(2u, responses.size());
  EXPECT_EQ(2u, responses[1].connection_id);
  EXPECT_EQ(kDeadlineExceeded, responses[1].header.responseStatus());
}
TEST(RpcServer, SlowSynchronousHandlerCannotSendSuccessAfterDeadline) {
  TestMessaging messaging;
  FunctionTable functions = {
      {1, [](RequestHandle h, const roo::byte*, size_t, bool) {
         roo::this_thread::sleep_for(roo_time::Millis(40));
         h.sendSuccessResponse(nullptr, 0, true);
       }}};
  RpcServer server(messaging, &functions);
  server.begin();
  messaging.request(1, RpcHeader::NewUnaryRequest(1, 7, 10));
  auto responses = messaging.awaitResponses(1);
  server.end();
  ASSERT_EQ(1u, responses.size());
  EXPECT_EQ(kDeadlineExceeded, responses[0].header.responseStatus());
  EXPECT_EQ(1u, messaging.awaitResponses(1).size());
}
}  // namespace
}  // namespace roo_transport
