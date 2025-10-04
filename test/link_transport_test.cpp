#include "roo_transport/link/link_transport.h"

#include "gtest/gtest.h"
#include "roo_transport/link/link_loopback.h"
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

// Loopback with receive threads.
class AsyncLoopback : public LinkLoopback {
 public:
  AsyncLoopback() {
    begin();
    t1_ = roo::thread([this]() {
      while (serverReceive()) {
      }
    });
    t2_ = roo::thread([this]() {
      while (clientReceive()) {
      }
    });
  }

  ~AsyncLoopback() {
    close();
    t1_.join();
    t2_.join();
  }

 private:
  roo::thread t1_;
  roo::thread t2_;
};

// Testing the happy path.
TEST(LinkTransport, SimpleConnectSendDisconnect) {
  AsyncLoopback loopback;

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
  AsyncLoopback loopback;

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

std::unique_ptr<roo::byte[]> make_large_buffer(size_t size) {
  std::unique_ptr<roo::byte[]> buf(new roo::byte[size]);
  for (size_t i = 0; i < size; i++) {
    buf[i] = roo::byte(rand() % 256);
  }
  return buf;
}

TEST(LinkTransport, LargeRequestResponse) {
  AsyncLoopback loopback;
  auto request = make_large_buffer(1000000);
  auto response = make_large_buffer(2000000);

  roo::thread server([&]() {
    Link server = loopback.client().connect();
    roo_io::InputStream& in = server.in();
    roo_io::OutputStream& out = server.out();
    EXPECT_EQ(server.status(), LinkStatus::kConnected);
    size_t request_byte_idx = 0;
    while (request_byte_idx < 1000000) {
      EXPECT_EQ(in.status(), roo_io::kOk);
      roo::byte buf[1000];
      size_t count = rand() % 1000 + 1;
      size_t n = in.read(buf, count);
      EXPECT_GT(n, 0);
      for (size_t i = 0; i < n; i++) {
        EXPECT_EQ(buf[i], request[request_byte_idx + i]);
      }
      request_byte_idx += n;
    }
    EXPECT_EQ(in.status(), roo_io::kOk);
    EXPECT_EQ(out.status(), roo_io::kOk);
    size_t response_byte_idx = 0;
    while (response_byte_idx < 1000000) {
      EXPECT_EQ(out.status(), roo_io::kOk);
      size_t count = rand() % 1000 + 1;
      if (count > 1000000 - response_byte_idx) {
        count = 1000000 - response_byte_idx;
      }
      size_t n = out.write(&response[response_byte_idx], count);
      EXPECT_GT(n, 0);
      response_byte_idx += n;
    }
    out.close();
    EXPECT_EQ(out.status(), roo_io::kClosed);
  });

  Link client = loopback.server().connect();
  EXPECT_EQ(client.status(), LinkStatus::kConnected);
  roo_io::InputStream& in = client.in();
  roo_io::OutputStream& out = client.out();
  EXPECT_EQ(in.status(), roo_io::kOk);
  EXPECT_EQ(out.status(), roo_io::kOk);
  size_t request_byte_idx = 0;
  while (request_byte_idx < 1000000) {
    size_t count = rand() % 1000 + 1;
    if (count > 1000000 - request_byte_idx) {
      count = 1000000 - request_byte_idx;
    }
    size_t n = out.write(&request[request_byte_idx], count);
    EXPECT_GT(n, 0);
    request_byte_idx += n;
    LOG(INFO) << "Client sent " << request_byte_idx << " bytes";
  }
  out.close();
  EXPECT_EQ(out.status(), roo_io::kClosed);
  roo::byte buf[1000];
  size_t response_byte_idx = 0;
  while (response_byte_idx < 1000000) {
    EXPECT_EQ(in.status(), roo_io::kOk);
    size_t count = rand() % 1000 + 1;
    size_t n = in.read(buf, count);
    EXPECT_GT(n, 0);
    for (size_t i = 0; i < n; i++) {
      EXPECT_EQ(buf[i], response[response_byte_idx + i]);
    }
    response_byte_idx += n;
    LOG(INFO) << "Client received " << response_byte_idx << " bytes";
  }
  EXPECT_EQ(in.read(buf, 1), 0);
  EXPECT_EQ(in.status(), roo_io::kEndOfStream);

  LOG(INFO) << "Client done, waiting for server";
  server.join();
  LOG(INFO) << "Server done";
}

}  // namespace roo_transport