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

// Testing the happy path.
TEST(LinkTransport, SimpleConnectSendDisconnect) {
  LinkLoopback loopback;
  // Start sender threads.
  loopback.begin();

  // Start receiver threads.
  roo::thread t1([&]() {
    while (loopback.receive1()) {
    }
  });

  roo::thread t2([&]() {
    while (loopback.receive2()) {
    }
  });

  Link server = loopback.t2().connectAsync();
  EXPECT_EQ(server.status(), LinkStatus::kConnecting);
  EXPECT_EQ(server.in().status(), roo_io::kOk);
  EXPECT_EQ(server.out().status(), roo_io::kOk);
  Link client = loopback.t1().connect();
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

  loopback.close();

  t1.join();
  t2.join();
}

}  // namespace roo_transport