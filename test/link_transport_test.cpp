#include "roo_transport/link/link_transport.h"

#include "gtest/gtest.h"
#include "helpers/link_loopback.h"
#include "helpers/rand.h"
#include "roo_threads/mutex.h"
#include "roo_transport/link/link_transport.h"
namespace roo_transport {

class NullPacketSender : public PacketSender {
 public:
  void send(const roo::byte* buf, size_t len) override {}
};

class NullPacketReceiver : public PacketReceiver {
 public:
  size_t tryReceive(const ReceiverFn& receiver_fn) override { return 0; }
  size_t receive(const ReceiverFn& receiver_fn) override { return 0; }
};

TEST(LinkTransport, DefaultConstructedLinkIsIdle) {
  Link link;
  EXPECT_EQ(link.status(), LinkStatus::kIdle);
  EXPECT_EQ(link.in().status(), roo_io::kClosed);
  EXPECT_EQ(link.out().status(), roo_io::kClosed);
}

TEST(LinkTransport, TransportConstructedLinkIsConnecting) {
  NullPacketSender sender;
  NullPacketReceiver receiver;
  LinkTransport transport(sender);
  Link link = transport.connectAsync();
  EXPECT_EQ(link.status(), LinkStatus::kConnecting);
  EXPECT_EQ(link.in().status(), roo_io::kOk);
  EXPECT_EQ(link.out().status(), roo_io::kOk);
}

// Testing the happy path.
TEST(LinkTransport, SimpleConnectSendDisconnect) {
  LinkLoopback loopback;

  Link server = loopback.client().connectAsync();
  EXPECT_EQ(server.status(), LinkStatus::kConnecting);
  EXPECT_EQ(server.in().status(), roo_io::kOk);
  EXPECT_EQ(server.out().status(), roo_io::kOk);
  Link client = loopback.server().connect();
  EXPECT_EQ(client.status(), LinkStatus::kConnected);
  EXPECT_EQ(client.in().status(), roo_io::kOk);
  EXPECT_EQ(client.out().status(), roo_io::kOk);
  server.awaitConnected();
  EXPECT_EQ(server.status(), LinkStatus::kConnected);
  EXPECT_EQ(server.in().status(), roo_io::kOk);
  client.out().writeFully((const roo::byte*)"Request", 8);
  client.out().close();
  EXPECT_EQ(client.out().status(), roo_io::kClosed);
  roo::byte buf[10];
  size_t n = server.in().readFully(buf, 10);
  EXPECT_EQ(n, 8);
  EXPECT_EQ(memcmp(buf, "Request", 8), 0);
  EXPECT_EQ(server.in().status(), roo_io::kEndOfStream);
  server.out().writeFully((const roo::byte*)"Response", 9);
  server.out().close();
  EXPECT_EQ(server.out().status(), roo_io::kClosed);
  n = client.in().readFully(buf, 10);
  EXPECT_EQ(n, 9);
  EXPECT_EQ(memcmp(buf, "Response", 9), 0);
  EXPECT_EQ(client.in().status(), roo_io::kEndOfStream);

  client.disconnect();
  EXPECT_EQ(client.status(), LinkStatus::kIdle);
  server.disconnect();
  EXPECT_EQ(server.status(), LinkStatus::kIdle);
}

TEST(LinkTransport, SyncConnect) {
  LinkLoopback loopback;

  roo::thread server([&]() {
    Link server = loopback.client().connect();
    EXPECT_EQ(server.status(), LinkStatus::kConnected);
    EXPECT_EQ(server.in().status(), roo_io::kOk);
    EXPECT_EQ(server.out().status(), roo_io::kOk);
    roo::byte buf[10];
    size_t n = server.in().readFully(buf, 10);
    EXPECT_EQ(n, 8);
    EXPECT_EQ(memcmp(buf, "Request", 8), 0);
    EXPECT_EQ(server.in().status(), roo_io::kEndOfStream);
    server.out().writeFully((const roo::byte*)"Response", 9);
    server.out().close();
    EXPECT_EQ(server.out().status(), roo_io::kClosed);
  });
  Link client = loopback.server().connect();
  EXPECT_EQ(client.status(), LinkStatus::kConnected);
  EXPECT_EQ(client.in().status(), roo_io::kOk);
  EXPECT_EQ(client.out().status(), roo_io::kOk);
  client.out().writeFully((const roo::byte*)"Request", 8);
  client.out().close();
  EXPECT_EQ(client.out().status(), roo_io::kClosed);
  roo::byte buf[10];
  size_t n = client.in().readFully(buf, 10);
  EXPECT_EQ(n, 9);
  EXPECT_EQ(memcmp(buf, "Response", 9), 0);
  EXPECT_EQ(client.in().status(), roo_io::kEndOfStream);

  server.join();
}

class TransferTest : public ::testing::TestWithParam<int> {
 protected:
  TransferTest() : loopback_() {}

  ~TransferTest() { join(); }

  void setServerOutputErrorRate(int error_rate) {
    loopback_.setServerOutputErrorRate(error_rate);
  }

  void setClientOutputErrorRate(int error_rate) {
    loopback_.setClientOutputErrorRate(error_rate);
  }

  void join() {
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    if (client_thread_.joinable()) {
      client_thread_.join();
    }
  }

  void server(
      std::function<void(roo_io::InputStream& in, roo_io::OutputStream& out)>
          fn) {
    roo::thread::attributes server_attrs;
    server_attrs.set_name("server");
    server_thread_ = roo::thread(server_attrs, [this, fn]() {
      Link server = loopback_.server().connect();
      ASSERT_EQ(server.status(), LinkStatus::kConnected);
      ASSERT_EQ(server.in().status(), roo_io::kOk);
      ASSERT_EQ(server.out().status(), roo_io::kOk);
      fn(server.in(), server.out());
      server.out().close();
      ASSERT_EQ(server.out().status(), roo_io::kClosed);
    });
  }

  void client(
      std::function<void(roo_io::InputStream& in, roo_io::OutputStream& out)>
          fn) {
    roo::thread::attributes client_attrs;
    client_attrs.set_name("client");
    client_thread_ = roo::thread(client_attrs, [this, fn]() {
      Link client = loopback_.client().connect();
      ASSERT_EQ(client.status(), LinkStatus::kConnected);
      ASSERT_EQ(client.in().status(), roo_io::kOk);
      ASSERT_EQ(client.out().status(), roo_io::kOk);
      fn(client.in(), client.out());
      client.out().close();
      ASSERT_EQ(client.out().status(), roo_io::kClosed);
    });
  }

  LinkLoopback loopback_;
  roo::thread server_thread_;
  roo::thread client_thread_;
};

std::unique_ptr<roo::byte[]> make_large_buffer(size_t size) {
  std::unique_ptr<roo::byte[]> buf(new roo::byte[size]);
  for (size_t i = 0; i < size; i++) {
    buf[i] = roo::byte(rand() % 256);
  }
  return buf;
}

TEST_P(TransferTest, LargeRequestResponse) {
  int error_rate = GetParam();
  if (error_rate > 0) {
    setServerOutputErrorRate(error_rate);
    setClientOutputErrorRate(error_rate);
  }

  const size_t kRequestSize = 200000;
  const size_t kResponseSize = 500000;
  auto request = make_large_buffer(kRequestSize);
  auto response = make_large_buffer(kResponseSize);

  server([&](roo_io::InputStream& in, roo_io::OutputStream& out) {
    size_t request_byte_idx = 0;
    while (request_byte_idx < kRequestSize) {
      EXPECT_EQ(in.status(), roo_io::kOk);
      roo::byte buf[1000];
      size_t count = rand() % 1000 + 1;
      size_t n = in.read(buf, count);
      ASSERT_GT(n, 0);
      for (size_t i = 0; i < n; i++) {
        EXPECT_EQ(buf[i], request[request_byte_idx + i]);
      }
      request_byte_idx += n;
    }
    EXPECT_EQ(in.status(), roo_io::kOk);
    EXPECT_EQ(out.status(), roo_io::kOk);
    size_t response_byte_idx = 0;
    while (response_byte_idx < kResponseSize) {
      EXPECT_EQ(out.status(), roo_io::kOk);
      size_t count = rand() % 1000 + 1;
      if (count > kResponseSize - response_byte_idx) {
        count = kResponseSize - response_byte_idx;
      }
      size_t n = out.write(&response[response_byte_idx], count);
      ASSERT_GT(n, 0);
      response_byte_idx += n;
    }
  });

  client([&](roo_io::InputStream& in, roo_io::OutputStream& out) {
    size_t request_byte_idx = 0;
    while (request_byte_idx < kRequestSize) {
      size_t count = rand() % 1000 + 1;
      if (count > kRequestSize - request_byte_idx) {
        count = kRequestSize - request_byte_idx;
      }
      size_t n = out.write(&request[request_byte_idx], count);
      ASSERT_GT(n, 0);
      request_byte_idx += n;
    }
    out.close();
    EXPECT_EQ(out.status(), roo_io::kClosed);
    roo::byte buf[1000];
    size_t response_byte_idx = 0;
    while (response_byte_idx < kResponseSize) {
      EXPECT_EQ(in.status(), roo_io::kOk);
      size_t count = rand() % 1000 + 1;
      size_t n = in.read(buf, count);
      ASSERT_GT(n, 0);
      for (size_t i = 0; i < n; i++) {
        EXPECT_EQ(buf[i], response[response_byte_idx + i]);
      }
      response_byte_idx += n;
    }
    EXPECT_EQ(in.read(buf, 1), 0);
    EXPECT_EQ(in.status(), roo_io::kEndOfStream);
  });

  join();
}

INSTANTIATE_TEST_SUITE_P(RequestResponse, TransferTest,
                         ::testing::Values(0, 1, 2, 10)  // Error rates to test.
);

TEST_P(TransferTest, BidiStreaming) {
  int error_rate = GetParam();
  if (error_rate > 0) {
    setServerOutputErrorRate(error_rate);
    setClientOutputErrorRate(error_rate);
  }

  const size_t kRequestSize = 200000;
  const size_t kResponseSize = 200000;
  auto request = make_large_buffer(kRequestSize);
  auto response = make_large_buffer(kResponseSize);

  server([&](roo_io::InputStream& in, roo_io::OutputStream& out) {
    roo::thread server_recv([&]() {
      roo::byte buf[1000];
      size_t request_byte_idx = 0;
      while (request_byte_idx < kRequestSize) {
        EXPECT_EQ(in.status(), roo_io::kOk);
        size_t count = rand() % 1000 + 1;
        size_t n = in.read(buf, count);
        ASSERT_GT(n, 0);
        for (size_t i = 0; i < n; i++) {
          EXPECT_EQ(buf[i], request[request_byte_idx + i]);
        }
        request_byte_idx += n;
      }
      EXPECT_EQ(in.read(buf, 1), 0);
      EXPECT_EQ(in.status(), roo_io::kEndOfStream);
    });
    size_t response_byte_idx = 0;
    while (response_byte_idx < kResponseSize) {
      EXPECT_EQ(out.status(), roo_io::kOk);
      size_t count = rand() % 1000 + 1;
      if (count > kResponseSize - response_byte_idx) {
        count = kResponseSize - response_byte_idx;
      }
      size_t n = out.write(&response[response_byte_idx], count);
      ASSERT_GT(n, 0);
      response_byte_idx += n;
    }
    out.close();
    EXPECT_EQ(out.status(), roo_io::kClosed);

    server_recv.join();
  });

  client([&](roo_io::InputStream& in, roo_io::OutputStream& out) {
    roo::thread client_recv([&]() {
      roo::byte buf[1000];
      size_t response_byte_idx = 0;
      while (response_byte_idx < kResponseSize) {
        EXPECT_EQ(in.status(), roo_io::kOk);
        size_t count = rand() % 1000 + 1;
        size_t n = in.read(buf, count);
        ASSERT_GT(n, 0);
        for (size_t i = 0; i < n; i++) {
          EXPECT_EQ(buf[i], response[response_byte_idx + i]);
        }
        response_byte_idx += n;
      }
      EXPECT_EQ(in.read(buf, 1), 0);
      EXPECT_EQ(in.status(), roo_io::kEndOfStream);
    });

    size_t request_byte_idx = 0;
    while (request_byte_idx < kRequestSize) {
      size_t count = rand() % 1000 + 1;
      if (count > kRequestSize - request_byte_idx) {
        count = kRequestSize - request_byte_idx;
      }
      size_t n = out.write(&request[request_byte_idx], count);
      ASSERT_GT(n, 0);
      request_byte_idx += n;
    }
    out.close();
    EXPECT_EQ(out.status(), roo_io::kClosed);

    client_recv.join();
  });

  join();
}

INSTANTIATE_TEST_SUITE_P(BidiStreaming, TransferTest,
                         ::testing::Values(0, 1, 2, 10)  // Error rates to test.
);

}  // namespace roo_transport